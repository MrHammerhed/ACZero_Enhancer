#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ACZ_SWAD_Converter.py — Ace Combat Zero: The Belkan War (PC Recompilation)
SWAD (.swa) Menu Unpacker, Repacker & Widescreen (16:9 / 21:9) Patcher for All 31 Menus.

Supports:
  - Unpacking .swa archives into chunk00_layout.swg + chunkNN_texture.tm2 + manifest.json
  - Repacking .swa archives with 16-byte chunk alignment
  - Surgical 16:9 (682x448) and 21:9 (896x448) widescreen layout patching for all 31 menus
  - Automatic extraction of entry_NNN_*.png + .json and generation of 9-slice _16x9.png / _21x9.png
    widescreen frame & grid textures (with zero .tm2 or manifest.json clutter left in menu folders)
"""

import argparse
import glob
import json
import os
import shutil
import struct
from typing import Dict, List, Set, Tuple


def unpack_swad(swa_path: str, out_dir: str):
    with open(swa_path, "rb") as f:
        data = f.read()

    if data[:4] != b"SWAD":
        raise ValueError(f"File {swa_path} is not a valid SWAD (.swa) archive!")

    num_items = struct.unpack_from("<I", data, 4)[0]
    os.makedirs(out_dir, exist_ok=True)

    manifest = {
        "source_file": os.path.basename(swa_path),
        "num_items": num_items,
        "chunks": []
    }

    for i in range(num_items):
        off, size = struct.unpack_from("<II", data, 8 + i * 8)
        chunk_data = data[off:off + size]
        magic = chunk_data[:4]

        if magic == b"SWG\x00":
            ext = "swg"
            role = "layout"
            internal_name = chunk_data[8:0x48].split(b"\x00")[0].decode("latin1", errors="ignore")
            w, h = struct.unpack_from("<HH", chunk_data, 0x50)
            extra = {"internal_name": internal_name, "canvas_width": w, "canvas_height": h}
        elif magic == b"TIM2":
            ext = "tm2"
            role = "texture"
            w, h = struct.unpack_from("<HH", chunk_data, 0x24)
            extra = {"width": w, "height": h}
        else:
            ext = "bin"
            role = "raw"
            extra = {}

        fname = f"chunk{i:02d}_{role}.{ext}"
        with open(os.path.join(out_dir, fname), "wb") as cf:
            cf.write(chunk_data)

        chunk_info = {"index": i, "filename": fname, "role": role, "original_offset": off, "size": size}
        chunk_info.update(extra)
        manifest["chunks"].append(chunk_info)

    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as mf:
        json.dump(manifest, mf, indent=2)

    print(f"[+] Unpacked {num_items} chunk(s) from {os.path.basename(swa_path)} -> {out_dir}")


def repack_swad_from_buffers(chunk_buffers: List[bytes], out_swa_path: str):
    num_items = len(chunk_buffers)
    header_size = 8 + num_items * 8
    current_off = (header_size + 15) & ~15

    chunk_table = bytearray(num_items * 8)
    chunk_payloads = []

    for i, cdata in enumerate(chunk_buffers):
        current_off = (current_off + 15) & ~15
        csize = len(cdata)
        struct.pack_into("<II", chunk_table, i * 8, current_off, csize)
        chunk_payloads.append((current_off, cdata))
        current_off += csize

    out_buf = bytearray(current_off)
    out_buf[0:4] = b"SWAD"
    struct.pack_into("<I", out_buf, 4, num_items)
    out_buf[8:8 + len(chunk_table)] = chunk_table

    for off, cdata in chunk_payloads:
        out_buf[off:off + len(cdata)] = cdata

    os.makedirs(os.path.dirname(os.path.abspath(out_swa_path)), exist_ok=True)
    with open(out_swa_path, "wb") as f:
        f.write(out_buf)


def repack_swad(manifest_path: str, out_swa_path: str):
    in_dir = os.path.dirname(manifest_path)
    with open(manifest_path, "r", encoding="utf-8") as mf:
        manifest = json.load(mf)

    chunk_buffers = []
    for c in manifest["chunks"]:
        p = os.path.join(in_dir, c["filename"])
        with open(p, "rb") as cf:
            chunk_buffers.append(cf.read())

    repack_swad_from_buffers(chunk_buffers, out_swa_path)
    print(f"[+] Repacked {len(chunk_buffers)} chunk(s) into {out_swa_path}")


def _resolve_mat_idx(swg: bytes, off: int) -> int:
    op = struct.unpack_from("<i", swg, off)[0]
    if op in (0, 1):
        return _resolve_mat_idx(swg, struct.unpack_from("<i", swg, off + 8)[0])
    elif op == 2:
        return struct.unpack_from("<i", swg, off + 12)[0]
    elif op == 3:
        return struct.unpack_from("<i", swg, off + 8)[0]
    elif op in (4, 5):
        return struct.unpack_from("<i", swg, off + 16)[0]
    return -1


def _load_sym_pool(swg: bytes, header_off: int) -> List[str]:
    pool_cnt, pool_off = struct.unpack_from("<2I", swg, header_off)
    if pool_off == 0 or pool_cnt == 0 or pool_off + pool_cnt * 4 > len(swg):
        return []
    res = []
    for i in range(pool_cnt):
        soff = struct.unpack_from("<I", swg, pool_off + i * 4)[0]
        if 0 < soff < len(swg):
            end = swg.find(b"\x00", soff)
            res.append(swg[soff:end].decode("latin1", errors="ignore") if end >= soff else "")
        else:
            res.append("")
    return res


def _resolve_menu_name(swg: bytes) -> str:
    raw = bytes(swg[8:0x48]).split(b"\x00")[0].decode("latin1", errors="ignore")
    if raw == "AircraftSelect_":
        return "AircraftSelect_Q"
    if raw == "ControlSet":
        t0_cnt = struct.unpack_from("<I", swg, 0x58)[0]
        if t0_cnt == 541:
            return "LanguageSet"
    return raw


def _collect_mc_mats(buf: bytes, dict_entries: List[Tuple[int, int]], mc_list: List[int]) -> Set[int]:
    ms: Set[int] = set()
    for di in mc_list:
        dtype, doff = dict_entries[di]
        if dtype != 5:
            continue
        _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
        for fi in range(frm_cnt):
            boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    ms.add(_resolve_mat_idx(buf, p))
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    ms.add(_resolve_mat_idx(buf, poff))
                    p += 16
                elif op in (4, 5):
                    ms.add(_resolve_mat_idx(buf, p))
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                elif op == 7:
                    p += 16
                else:
                    p += 4
    ms.discard(0)
    ms.discard(-1)
    return ms


_MAINSELECT_3OPT_XOR_B85 = "c-rmV3-n#nz3B1zkG;u%|C5R%tsp8Y)U$$=C?zCbm3l@SL`6lVm6o<CX{8Pl1d#+m5QKVGkZQbxAc%KV5Tq6Ak$MJ6ghq3IYtMPcxaW?0?l|Y@y*)kse8!mLYp%W4UVE>#_Uk`rQ~KK(zs4G=CEBHH?q5?q#?((%OaK4?0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000{C#S12mtU|1ONa40000000000000000000000000000000Dl+!e}RJ6CIA2c000000000000000000000000000000000000000000000008{|*5VKV00000@OP@ye_)-iDgP~ew-_77m=I&L80}->SE7v=Yo+|pTgKSFjP^0UT>gk&t>3O}zjln3VtgrXTRr+<jMj2nYuV=}aa;fBHr0K^=<8Gg00000000000000000000000000000000000000000000000000008`_wZs_FzV1K(00000000000002+SJ(V+SJFxj1i)u}FCF!}ngam<00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000093F&DnT`w1UF`0000000000000000000000000@Hf$Z&I+j|+9A49bZF_@rTzYwR_iT$E_X`lq_TWW>GV1OcKL|Xfu-A(cKz4;dnR{Y>GaY`r6WoQmTp(twRG7dW&6_UrISiWlnyN2uC#0Ezo+&{e%>3D^_G_IRyv?`!Q;8Nl@53!cX(-+(q5%=OP7{TEWNFCz>{VB(k`XFO6Qg?EuC0;Tj_vVW&6@DrM*h$mM$%wIP3p@9R5*so8f;e`9JG~8Ra!L!!-l|00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000Kh+vVWYR%eCy2*=`rz)_Q(I?_~X;$(|Yyj+2fENYm98a+gCT=eDecd>(yiPwEH(Fp5E(_o;|imS8P>JyN~IZe!g`*?H%1AIw1OR^t;iK(IcY=^{A(xMpumvFY}Y4zuLT>PLIA7JtO+imi2UU^oV}-ba!;zp7nG~bnCt9>4xYbyVcWG(Nn)sPk)FWwMRWY8+~r)dU_&y&o1@!VD!to*3)02PwrSxOQOf_R8Q|l*X~nKuSa(sSWl~+*)dH!q@GrYZgN09Z5q8~Ks_B29e!Xv?HRpb|9a{Z-TvG4^!@1IZ`IRz(Kq+5rz@g^qBlkVuwOmRs_rlPdUW6DvS|B#>ZvidW7;L!EjlT>ee{lK|7fQl#c_(h8yy}!JUYHw@2GmZHrg|KZ?r@7`RL4{@qD5uMLUe^nCAYlp4NzN74sg^O``pxW1~kz`yW|PKaQ>uJtI0OdS!IU59;Zj=!odU(fht%Pk)S#j(!^b!!dDu#&=BJkFBS5qQ8!A8=V^6Kf2=ZdO9llSoG)7vwvPs=S1g5uaEvH`cQPyFY4*3=yA~n(RR@l&gz(UkFF6NAN^`{{a@D8UeQgW2Sjg*{xmv$L_LkEZjW9S9sSSoxT8Oa&Wyf#d_8>>{m!BF^iK5c!|JK)*|DFPZxtQ>y?WXpy8RLLv`2LD;CebPdfnmm^uy?haXe0qUOl9qZjb&xp8wO){o*`45Zy42`|{|@aXeQ!r(;_0oLXv&9(Q&v^@^T+RxRxl?Q?!D?G@eTyjnUu`rNs-bWZe#6Kd)7=!X~9(p}MyFQ}!5qHj#BrKh5gUs_9lj=ptCE!8J<Osm9tn?+xWZWF!oqFU+`9XYC&hE%toTuVQRUV2I`jf$=pJwN*Csd4|&&7*&c4nColRzEi$e{|L8N6|f_+m5WI-qC$es-*$ZH>3MTcQ~__21iehj*YH5rj{;_P8wHBmqm|?-V(iIY%R@<K6+X$y%pU%`f2n#XVlV4=f(MmZWz63bS-Te9Uiyu96deyqiCmFYH4`%`?uE8kE6Y!Cq?g{7W<3t61_J1$j!BMPjsJ~YU#!3LsM(%ld8X|rOxMfOy9VtmbQ)dxVx4Ph(3B(EgctqczP{eRNa1OE!`g7<&Ii<Iy&#RT6#44?A5jOa`d{ZYH5v$@p|}mEwztMyRMeDi=Hv1miCVR_WC$p(F1Ob;}UH*xt7k3ZgyEMjf)Pryq2zvF1;eoXY`+=&qSA9SxYZOkBz<?eJl3!L3CVn#S7x~5c}^GJtpQ~i7t+QJ^GX9<NArd7VRJX>D+ie(KVviM*BqXk6!s=EzOShioO@U`h{9r7`;8VYg`!T{|~kFrRc5Et*ZG;ar~oOM)!-}96dDJV_q#C6P+48rRwi%X-ste=y}mGzpJH7qU%L(k8TuwJ$m8Oah+Zi*KN$#iw=IOmbQ;BiVlb#JiC@gMn4z5JbKr2alJ&Zk3Jc#NAHcU|7<P2A6?~{TKZ#jwK;K|qEA0oOJAJSG5zlGIM2}uPt?-J(J@cf(z?+rXT^0Do%L`n9TeU8ky<(|y4IudJfbsZ*3$LScOR&wJEDsotfdE|A3jt|OQLtoi2J=bj%Rf1=)n7HX<&4-`)X-KG<_PctLWF4)zWXGGnU8eG<sHaY4r72{`pHfroCdlHKT7wH;%S^6303EYIL9I@ITkm(CCNJ6QaYS=SCMUt)&a1Bcj(wmwp`kiLMZRFgkxpEzOF4GdeH&r;loBe)QbvqUb8orO|<lYpLU<9n-zhwWEhDs-<<J7e}{@uK8g+p6K(@Z%4oNK`k8^?HnB%Ju7;2bkq0a`9^n-j*ecrFkU~=di18~bMM9V7CkJwB>H}w*Y=Y;rk&z^Zyg;U?H^qyu7_iyTSm`~j*RPMYV@G!3(>c$^BWx(?RZ(7mpJdMM~BD!tI=iA9ixNd`WP7fH2TlcUqmm7E{W^qSJ5rvI)5p8UtIr-qSNF0e>plh_Ve+L9n+4n|5u}5jK{UY)Ob8S;{7PvbzZwP?BKT4^`&-caP&)mXqUbo-*;w=YE7N1FFvQVrd1<zPi;+&=$@ln)0)xor?sX&Eh$}gdTZJ?=9`RZP5%@<^UT)t`Do9vt*Jw_-T2n@#ptNBTGPI<{sm{{^%`gA^?n+yM`uRgiFQ1vHEkRndQM*cN-W<ky3vHz6jx#DAKf+X=b7k+)qc-yZGJzyA-Ym@(Rq1)mq%BQE{onD>#uWuYw8s952C9@`%P?3hsN?dqbtUIs|)h|O}HT6&kruj_p|SX`F`HIFz-KIl#k0x(XU1)P0HI%o0PXZb5edhXI`A=3n%6K+3AveKl7qri~ZIw&D%}6G_UvCW%+Tu8C|=2K9gJ1UeWJfmXG(G%UjbivEA*z%E#qvSGJ~h)p5S6H4Tg9{jbjb;We%4fLK2E+ScZIoV5Rv<=aIcyDlI1PE+!6|NISkKJUiX)E524)Yh~{^*#4Dt?8M#pXYCCO)JFwiQne)*zM-l^js`I;+DMq<Xc-)mzaNiTR#8Y?#Pd8=Q~?dzv%0C<m1&P=4-_RUFXhxUZ>uf*E{mAd|vmOUY1YK%lE%4Kd;^I%JT`ayleEqyYl_@x;r1gkMGXMZ-aaC@%u35--xa@BQGB^BcF#?O82=pueZUy`FwowzI;9=M}HIdckuoB`Z(eKy#0dv^ZM^UkhgzoW}f$(nYX{>fxLaU2lL~3{=vLm`-k#+e~$T{)$w~MuebNC*7Q`YciODHUayDqdNXI`$Gy@c`TT$9k$nHNqpwA`el$O>(T}yJrO~q<Z%x}*kMoJVe6Q%i(MxCN<>Q~q%b$EYU+1Sio#&&TX-(BLxhmSFy1!>z)28uw?tQM@-`u?3{d4p6bJYv^dYJZNzV0rVm(Rxwzt8UjonLBAyTtM>=jZEa;LCCUF~9lcd_EVwoY(*HAM^Ea(<}M>y&WAJ>;3GHdA(78%<DB?$?LUyHLo`!`n_21#8>nEZ}D2bo{xMjuYYGWZcaPAo{!r#(GBCmS^JHAT*tkUucM{W_o~OSARqVH3-aw_7v$r9<Dc?;%A2jJ7M=L!X*;fzPS~;iTd_QL(BsT@X{GjYotzYn6(*hBuDSl8-YYk|V8`6OOMCUnol!cvbm309T}me(oO{i8ayuQ8yEIxozd@y=2j=b_t&Yot=;xw+56bhQ2j&htVCClPbVTX!0lC9=%RPGc+@asd9aLJ|BX>x@+<v8LuiReayEOMVV$Zza0l(?cY;9`pjM6c&e3j_@TRJq4`}`x<Z1%QW^L#?-(dG6rrK3xS-JILu`+2)5Wj>_z)thoBmk#`G?yS<KL-KaL%Y0c`Z@^u-Jxgbob}j8y+WDUH_)0rOo98uJT^IAF=Z-I(c~@??GVf5jLFu$RbElS0pO!nTwDZlmok|Cm&b=jf)UCPmOZ%1OOG^ir&bT&r=&y4}m-fFdcg}VBc?>IEbWMlm>t%4XdOzqHtzP#%ZtT$foL_cBo_D)Gx9^nPYp%$hbY<=dSLI%Db?$DHb2qsxcj2#cdtRJ7d{S<|OLP10xn}cqJ#+WmeiJ)1J7kYMpR#jqxAQwRKW}Oi%K0drbZ+i|((dQv_9&fRdfVB#J$G6&zs|~h;Jf)a%zrn}2fvp)w{(-z+ZN`IdOx>!nRh51Q2OeJxkr~SE6dxL?PivaUy?hobc52Kr87#$l+ImT9$)FgMID;2%dX3EPgs^;e{X-<q4_y8^DntqEYGc#4qlqucWFNFy)JBNc1-E)i*mcanmhWn+<~RtUT<l>@ArHo_nP^+V_wdk{7P=$Xm!2@z0=bC+`OXnn$n?f=PoSmUwYenx${a>>CAU?mu<9Wv;E4v)0?@&N+*_1FYWeLZof_Qc0<a1%7WY;(dzq6*D_zaeqL|Ndbypt=XUrg-`=})ROx^vx!aY_UXt%`;^N%lJFMC4-eo>&QSQ;D%RbB<vMhH_Y5FwxvC`X0yZ<G3cIl+$xwA@#mCj$7JG8V*Y2Rpdy$>p#RN8-=HJj&cw-54p8@xE5$Kj<5w#&C4{ZXC|Tar8E;oN?+atA${yFp7{f4~!Ey_DyhlrAV;6s^8T&yFwWRp++P^SSrtdEW<fdq33D{2tSHW=r#PZuSGY)62a5i@6g@2fdK{Sm}W0bGw&LotxXWbiqq4`E&85JfHiA+-YU~ZsmR^z3^}M_iE|hbISfoZ+j-UbLk1s=8h;G7}r_VzR%@(kEhDxSa;3l>ug@9+{vrv4lDDyrBhbP9o@MlT^c>;ghtvT`tga4)F=AzNsY8$boG&q^p)swqZ+AOwDsgh8d<#`oYF|EM}HmNJ9@{djdXnU+0l*Eqxyb#S|jz2u5x;#c|9C|Mk9T>dL4{uq;0D6d1fP>Q2m}6+em|}-$&ybsb_V4jBlhbRlh6FYNQRKUq8E%4vIc-P9yCT{n>;@`dRg|o{+aY?%cfH>}cQWJfGJ{{j2wd=t<Fy&u=t;|GXGIB)Z4MMmj2b#>D)*wz;5@s^2w#j5c4X7dFyb)%UsR9??TC%KLvZdSvvxNsV+`^xf!T)$_Qxk-k^`K8RMow~o4`k=C!?uP(`t|B>jJ>Umw-NZq4LqFY6encPS_SM%up)%>zXI=s4`qsK%KzdRr3KVP1Y(;2^Nq_NfGi&md!$6nD$qoONDe^K54mH9YNiLP7yuDdFqhgnzU^DyA*e4Z9WtM6B%uW6(+qa&|vq+0bmGUh8rPmNZ;$69~gXuiIV-GA}&>N@$T|KjBzEnmL;*wT;oTfDq|bw5)YshT&g&pmeEa{E4umoJP?xv|mwyjZ^1;^o!#xmomsSboT!x#PZ>@2B@~8|j?tbw90<z7TIT^P<)An{Lj_@4qD;cPt-zYhK>{w!Hk-=*;+nvh4P}{Jc9FX<YTbcxNNM7|WMVZ!};3^X|&aufMyIUWnzh?rAjN4_3Ulk<P1r7v7uir^kJb{Cc}DFW>e4MtUHYPrW}cZ#Oe9pAmhqTK|E(e9{AX`Hl}Z(nHny59Z}xd?+u!IXbsme^y>TG5V|O_5E-oJzuT=a9+N~BYF8nkL1VM<IzU*ePv8^V)Z*Px<#CyA&)ik=SBQ}sNR=5Kb~*z8(k^hkROj$%eQ(WFTXuni{<Y;k(clNWFuW(9j9npEdS-~y#H5b=l%cmsn~BU|MaQ6eCY4;^LQru_1OO}pKdh2FFbZc{(e7rP9vQfU324nJ_kLMub*Mh<m=+FXY=!z|7^a#F4|!6^6K%=e=gt8;<<UfcjxB$218eFp3ga#wWMCd+fx6_TblF9(LG|``-<{@9qk|UIalWQ-)>j6q(LzsbanpR==OtrKV#-M(lyoJe_n2+`SC-j{VRDtL!v{XhrXV#pWR-~>mB)8BW)Rv^O5MB>+9+CxcyJjX>T;ry>b7Y$G0>)=TD8aIGWCGX?D?Djr3;Kb6e8F=;`mo{oYVd9o~)eAM=6dwKUt|y+(RFI^_J8W;-lwG}j+CI&ar_Ki)TPtfw*W=k4a5p0^wGLEdh`8F{-gALi{AjVb$E6wmrMvA@M-e`l8cEh+mOTlV)+BfTFTI<6&s8hs_^pO5XfTG~j@RLA>-mh@H}r)OjSa?EF(*pl9i`7s|in(J>D^L}xBzVJz2f7(g${NwgpV*XCdr;Kb#AIJQWKj+6cbyQ30TOH43`EgA=xg~v4-TrBQTvJYINjp~azm)Y)ZAk;F^_Q3RhsS$ib$e>d>;F8i-D=*_me-Fr!2_!G+qLEOkBe)!x_yPR{)m+RQq5~+{a?msUbTL`tREj<hsW(dSSgNe%x4}Q*Ko{#*S;-%81vyjj?a!*zh|SZ`FF0q(H~atOP_0NzQ66%p)K85U60X2V!JCk=H-jx_Mz2v6|Jt<4et6}^K*AZe3-0uTw8jhb6fNGQit!Sv`);YeId`g#D~=WG2gb_J}7qfxtRa(pW2$g<9o)3&(<*?yH=i0j(MM$cj%JmbH5v}#h5?2cAif-G_Swli+Mg{P+tG~HS&D;5qbR$SI_fd-^=TNquk%1!}I$8dVgI8=k?Dh+s_%C*YCf6Tk~}{IzBXajrnEkv^D>}(&NCC){gnq?s+~v=3B;mpEAGZz`Xvt>$as$tM4~oYHR-P91(4P?~GQT%g;uu*Lm+)Z(3~lKy;((`L3JS`=_|S>hrEowECRgyL-NULHB%nr}c8bReIKXc|YGN^MlvV-GBYOeE9l#zkgXj@3+&J+tPip-?hG+&&%D>>iffUU(VaDv_U=|TSu$k|AV6I#y_08VO!d<`hKuczJ7MwxGlXN-F%a_=J%?!X<PIC=qoX=zJI;+mA14_b^UCXZ=c$ut$E!H**tg8Epj*9GH=&?t31D|XYS))ZEOBM-mWbFWb3x(-|Ob|YD>>ne}~(qt@%6fldrWkzsJqnHr~_XaUQ;1Tl0HL=db7U*kil=cz&{dem={h)!%iW*`clZ{pQsj^7DOk$J|?cw>7_SJ<>a0?>%Grnz7-<(dzGX^U8YH^l5AUo$kP$@_I9)uT_6P?UUCV)+evG?M}Jf`sVxX-#6brsc(Kfi%NIeIrrS1^ZHNhn&&I;l6&GVdHHtH>U+|gyO!%~x3=_n?Dx>!+nRqL{$Tff9>;tm){70-+oLV5S3S=?@_AeBn{8>Q>hI~_%<C<S##jCH+Map6etYHhM(&e4W$%36cZzxSJ89s)abBwX-7lZl&i(WGJ9huJv}?8h{quR5`K`9}bagzwU5>}M%klVD?&k-T<1wJkzf=PN0000000000z~7C}IRyV>toT<x=j|L)00000000000000000000000000D%9q-YcgIf8OW6y!mw-{XV5`&pVF%bv!oaKenv+-uKo&F?H*4;%C}^&`Kvd@c+Qq&*b`Nx_-9kGv}1Q-}nC=PTb<BJDm6reg4P4cgknkzFm(GPWfA&ztp?SKlJ(QIr5#)bpCoC_WWnsJ{|qS-}L<V{P0UB{!Nens+;-z1ONa4000000000000000000000000000000000000000000000000000000000000007{BjuTe=jN5PH7y<wQ0000000000000000000000000000000000000000000000000000030CEp#>F&!M^TQT~__;!qGF^0$3EyiInj*4;rnH|&DqWi}<HpZwJC&zd)#-tdRm2Gd1adnI<N~grQw%q3}G4719d5oDcK8^8Mj7MTT9b<<W_r-WW##=Goh_Pwx_wwkgF`lm;M~tO0R*2=TF}@IEtr#1|*gVE+<2t6k(PLxmA7em_Lt>m1V`z*UVvLM&UW_B+eqN4Fj`2>6vtlfX@nVcW#rQbJ3S(nGG1iLFJw}fhtHpLZMt>0Fpcvnc@sk+m#uy*t&KTFkm=fdocs!>?AByoojQKG>tQz;xDQ^4Pxcrzmig9<fujt`1z7yln7`Ml=?W0p-TpVL!j7?(M4bf|3jE(VfjNiq0Eyla?m|ux*Fur42Jx13UQ_kv`j*G4uqj!w`VvLSu-J%D^I4Z{JG1iD>lcRUUSQg{n7ytkO0000000000000000Q~nc^|-dw;xPQx^#6By*#9;M0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000fd5YZ1wJQo>H"


def _upgrade_mainselect_to_3opt(swg_bytes: bytes) -> bytes:
    """Upgrades vanilla 272-entry MainSelect SWG (4:3 / 16:9) to 273-entry 3-option (4:3 / 16:9 / 21:9)."""
    if len(swg_bytes) != 1145464 or swg_bytes[:4] != b"SWG\x00":
        return swg_bytes
    t5_cnt = struct.unpack_from("<I", swg_bytes, 0x80)[0]
    if t5_cnt == 272:
        import zlib, base64
        xor_diff = zlib.decompress(base64.b85decode(_MAINSELECT_3OPT_XOR_B85))
        return bytes(a ^ b for a, b in zip(swg_bytes, xor_diff))
    return swg_bytes


def _patch_all_zz_setmovexy(buf: bytearray, dx_left: float, dx_right: float = 0.0) -> int:
    strings_pool = _load_sym_pool(buf, 0xD8)
    patched = 0
    if "zz_setMoveXY" in strings_pool:
        zsym = strings_pool.index("zz_setMoveXY")
        sig = struct.pack("<5i", 8, 0, zsym, 77, 23)
        pos = 0
        while True:
            idx = buf.find(sig, pos)
            if idx < 0:
                break
            if idx >= 72:
                ac_op, ac_val = struct.unpack_from("<Ii", buf, idx - 24)
                gv, pm = struct.unpack_from("<Ii", buf, idx - 8)
                if ac_op == 7 and ac_val == 6 and gv == 25 and pm == 46:
                    for k in range(5):
                        a_off = idx - 72 + k * 8
                        op_k, val_k = struct.unpack_from("<Ii", buf, a_off)
                        if op_k == 7:
                            struct.pack_into("<i", buf, a_off + 4, int(round(val_k + dx_left)))
                        elif op_k == 1:
                            fval_k = struct.unpack_from("<f", buf, a_off + 4)[0]
                            struct.pack_into("<f", buf, a_off + 4, fval_k + dx_left)
                    patched += 1
            pos = idx + 4

    if "zz_setMoveXYD" in strings_pool and abs(dx_right) > 0.01:
        zsym_d = strings_pool.index("zz_setMoveXYD")
        sig_d = struct.pack("<5i", 8, 0, zsym_d, 77, 23)
        pos = 0
        while True:
            idx = buf.find(sig_d, pos)
            if idx < 0:
                break
            if idx >= 80:
                ac_op, ac_val = struct.unpack_from("<Ii", buf, idx - 24)
                gv, pm = struct.unpack_from("<Ii", buf, idx - 8)
                id_op, dict_id = struct.unpack_from("<Ii", buf, idx - 32)
                if ac_op == 7 and ac_val == 7 and gv == 25 and pm == 46 and id_op == 7:
                    if dict_id in (143, 153):
                        for k in range(5):
                            a_off = idx - 80 + k * 8
                            op_k, val_k = struct.unpack_from("<Ii", buf, a_off)
                            if op_k == 7:
                                delta = dx_left if val_k < 250 else (dx_right if val_k >= 300 else 0.0)
                                struct.pack_into("<i", buf, a_off + 4, int(round(val_k + delta)))
                            elif op_k == 1:
                                fval_k = struct.unpack_from("<f", buf, a_off + 4)[0]
                                delta = dx_left if fval_k < 250.0 else (dx_right if fval_k >= 300.0 else 0.0)
                                struct.pack_into("<f", buf, a_off + 4, fval_k + delta)
                        patched += 1
            pos = idx + 4
    return patched


def patch_swg_to_widescreen(swg_bytes: bytes, aspect: str = "16x9") -> bytes:
    """
    Surgically patches SWG layout to 16:9 (682x448) or 21:9 (896x448) widescreen
    across all 31 game menus.
    """
    if _resolve_menu_name(swg_bytes) == "MainSelect":
        swg_bytes = _upgrade_mainselect_to_3opt(swg_bytes)
    buf = bytearray(swg_bytes)
    if buf[:4] != b"SWG\x00":
        raise ValueError("Not a valid SWG buffer!")

    if aspect == "16x9":
        target_width  = 682
        target_height = 448
        w_full        = 682.666687
        x0_left       = -85.333336
        K             = 4.0 / 3.0
        dx_left       = -77.0
        dx_right      = 76.666668
    elif aspect == "21x9":
        target_width  = 896
        target_height = 448
        w_full        = 896.0
        x0_left       = -192.0
        K             = 896.0 / 512.0   # 1.75
        dx_left       = -184.0
        dx_right      = 184.0
    else:
        raise ValueError(f"Unsupported aspect '{aspect}' (expected '16x9' or '21x9')")

    menu_name = _resolve_menu_name(buf)
    t0_off, t0_cnt = struct.unpack_from("<2I", buf, 0x54)
    t5_off, t5_cnt = struct.unpack_from("<2I", buf, 0x7C)
    dict_entries = [struct.unpack_from("<ii", buf, t5_off + i * 8) for i in range(t5_cnt)]

    # 1. Patch root canvas dimensions at +0x50
    struct.pack_into("<HH", buf, 0x50, target_width, target_height)

    # Special Handling for Boot/Intro/Title Menus: MemCheck, CM, Title, WakeLogo
    if menu_name == "MemCheck":
        print(f"[*] Patched 'MemCheck' SWG canvas to {aspect} ({target_width}x{target_height})")
        return bytes(buf)

    if menu_name == "CM":
        # Free Mat[241] on frame 239 of Dict[18, 20, 22] -> point to Mat[240]
        for di in (18, 20, 22):
            _, doff = dict_entries[di]
            _, _, _, frm_off, _ = struct.unpack_from("<5i", buf, doff)
            boff, _ = struct.unpack_from("<ii", buf, frm_off + 239 * 8)
            op_f = struct.unpack_from("<i", buf, boff)[0]
            if op_f == 2 and struct.unpack_from("<i", buf, boff + 12)[0] == 241:
                struct.pack_into("<i", buf, boff + 12, 240)
            elif op_f == 3 and struct.unpack_from("<i", buf, boff + 8)[0] == 241:
                struct.pack_into("<i", buf, boff + 8, 240)
        bg_mat_idx = 241
        struct.pack_into("<16f", buf, t0_off + bg_mat_idx * 64, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x0_left, 0, 0, 1)
        for di in (0, 1, 2, 3):
            _, doff = dict_entries[di]
            roff = doff + 4
            struct.pack_into("<f", buf, roff + 28, w_full)
            if di == 0:
                struct.pack_into("<i", buf, roff + 8, bg_mat_idx)
        for m in range(2, 241):
            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
            struct.pack_into("<f", buf, t0_off + m * 64 + 48, tx * K)
        print(f"[*] Patched 'CM' SWG to {aspect} ({target_width}x{target_height})")
        return bytes(buf)

    if menu_name == "Title":
        for di in (0, 1, 2, 3):
            _, doff = dict_entries[di]
            struct.pack_into("<f", buf, doff + 4 + 28, w_full)
        # Point frame 0 of Dict[10] from Mat[0] to Mat[2]
        _, doff10 = dict_entries[10]
        _, _, _, frm_off10, _ = struct.unpack_from("<5i", buf, doff10)
        boff0, _ = struct.unpack_from("<ii", buf, frm_off10)
        if struct.unpack_from("<i", buf, boff0)[0] in (4, 5) and struct.unpack_from("<i", buf, boff0 + 16)[0] == 0:
            struct.pack_into("<i", buf, boff0 + 16, 2)
        for m in range(2, 901):
            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
            struct.pack_into("<f", buf, t0_off + m * 64 + 48, x0_left + tx * K)
        for m in range(901, 1141):
            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
            struct.pack_into("<f", buf, t0_off + m * 64 + 48, tx * K)
        print(f"[*] Patched 'Title' SWG to {aspect} ({target_width}x{target_height})")
        return bytes(buf)

    if menu_name == "WakeLogo":
        # 1. Mat[0] is used exclusively by Dict[0] (16x16 black fade quad): expand to full widescreen width
        struct.pack_into("<f", buf, t0_off + 0, 32.0 * K)
        struct.pack_into("<f", buf, t0_off + 48, x0_left)
        # 2. Reuse Mat[2] as bg_mat_idx (sx=1.0, sy=1.0, tx=x0_left, ty=0.0) for Dict[1] (Bandai Namco white bg).
        #    Leaves Mat[3] (tx=-24, ty=85) and Mat[4] (tx=-32, ty=-64) 100% intact for license screen Dict[8].
        bg_mat_idx = 2
        struct.pack_into("<16f", buf, t0_off + bg_mat_idx * 64, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x0_left, 0, 0, 1)
        for di in (1, 2, 3, 4, 5):
            _, doff = dict_entries[di]
            if di == 1:
                # Dict[1]: Bandai Namco 512x448 white background -> start at x0_left, width = w_full
                struct.pack_into("<f", buf, doff + 4 + 28, w_full)
                struct.pack_into("<i", buf, doff + 4 + 8, bg_mat_idx)
            elif di == 2:
                # Dict[2]: Red Clouds background -> midx=1 (tx=0), width = 592.0 * K (w_full * 1.15625),
                # animated by Dict[13] via Mat[5..724] shifted below by x0_left + tx * K
                struct.pack_into("<f", buf, doff + 4 + 28, w_full * 1.15625)
                struct.pack_into("<i", buf, doff + 4 + 8, 1)
            else:
                # Dict[3, 4, 5]: 3 semi-transparent zooming cloud layers -> midx=1 (tx=0), width = w_full,
                # animated by Dict[15, 17, 19] via Mat[725..964] scaled below by tx * K around center (256, 224)
                struct.pack_into("<f", buf, doff + 4 + 28, w_full)
                struct.pack_into("<i", buf, doff + 4 + 8, 1)
        for m in range(5, 725):
            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
            struct.pack_into("<f", buf, t0_off + m * 64 + 48, x0_left + tx * K)
        for m in range(725, 965):
            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
            struct.pack_into("<f", buf, t0_off + m * 64 + 48, tx * K)
        print(f"[*] Patched 'WakeLogo' SWG to {aspect} ({target_width}x{target_height})")
        return bytes(buf)

    # =========================================================================
    # Standard 27 Menus:
    # =========================================================================
    # Find all used matrices and valid matrix slots in Table 0
    used_mats: Set[int] = {0}
    for di, (dtype, doff) in enumerate(dict_entries):
        if dtype == 0:
            cnt = struct.unpack_from("<i", buf, doff)[0]
            roff = doff + 4
            for _ in range(cnt):
                _, noff, midx, _ = struct.unpack_from("<4i", buf, roff)
                used_mats.add(midx)
                roff = noff
        elif dtype == 5:
            _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
            for fi in range(frm_cnt):
                boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
                p = boff
                for _ in range(opcnt):
                    op = struct.unpack_from("<i", buf, p)[0]
                    if op in (0, 1, 2, 3):
                        _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                        used_mats.add(_resolve_mat_idx(buf, p))
                        used_mats.add(_resolve_mat_idx(buf, poff))
                        p += 16
                    elif op in (4, 5):
                        used_mats.add(_resolve_mat_idx(buf, p))
                        p += 20 if op == 4 else 24
                    elif op in (6, 8, 9):
                        p += 8
                    elif op == 7:
                        p += 16
                    else:
                        p += 4
    used_mats.discard(-1)

    # Identify scanline matrices (sx=32.0, sy=1.0, tx=0.0) and full-screen fade matrices
    scanline_mats: List[int] = []
    fade_32_mats: List[int] = []
    fade_35_mats: List[int] = []
    valid_mats: List[int] = []
    for m in range(t0_cnt):
        vals = struct.unpack_from("<16f", buf, t0_off + m * 64)
        if abs(vals[10] - 1.0) < 1e-4 and abs(vals[15] - 1.0) < 1e-4:
            valid_mats.append(m)
            sx, sy, tx, ty = vals[0], vals[5], vals[12], vals[13]
            if abs(sx - 32.0) < 0.01 and abs(sy - 1.0) < 0.01 and abs(tx) < 0.01:
                scanline_mats.append(m)
            elif abs(sx - 32.0) < 0.01 and abs(sy - 28.0) < 0.01 and abs(tx - 256.0) < 0.1:
                fade_32_mats.append(m)
            elif abs(sx - 35.0) < 0.01 and abs(sy - 30.0) < 0.01 and abs(tx + 24.0) < 0.1:
                fade_35_mats.append(m)

    unused_valid = [m for m in valid_mats if m not in used_mats]

    # Select or free a matrix slot for bg_mat_idx (tx = x0_left)
    if menu_name == "MainSelect" and t0_cnt > 1566:
        bg_mat_idx = 1566
    elif menu_name == "AircraftSelect" and t0_cnt > 14:
        bg_mat_idx = 14
    elif unused_valid:
        bg_mat_idx = unused_valid[0]
    elif len(scanline_mats) >= 2:
        # Free the last scanline matrix (ty = -180.0, 100% off-screen above y=0)
        # by redirecting its op references to the second-to-last scanline matrix (ty = -174.7)
        last_scan_m = scanline_mats[-1]
        prev_scan_m = scanline_mats[-2]
        for di, (dtype, doff) in enumerate(dict_entries):
            if dtype == 5:
                _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
                for fi in range(frm_cnt):
                    boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
                    p = boff
                    for _ in range(opcnt):
                        op = struct.unpack_from("<i", buf, p)[0]
                        if op == 2 and struct.unpack_from("<i", buf, p + 12)[0] == last_scan_m:
                            struct.pack_into("<i", buf, p + 12, prev_scan_m)
                        elif op == 3 and struct.unpack_from("<i", buf, p + 8)[0] == last_scan_m:
                            struct.pack_into("<i", buf, p + 8, prev_scan_m)
                        elif op in (4, 5) and struct.unpack_from("<i", buf, p + 16)[0] == last_scan_m:
                            struct.pack_into("<i", buf, p + 16, prev_scan_m)
                        if op in (0, 1, 2, 3):
                            p += 16
                        elif op in (4, 5):
                            p += 20 if op == 4 else 24
                        elif op in (6, 8, 9):
                            p += 8
                        elif op == 7:
                            p += 16
                        else:
                            p += 4
        scanline_mats.pop()
        bg_mat_idx = last_scan_m
    else:
        bg_mat_idx = 0

    if bg_mat_idx > 0:
        m_bg_off = t0_off + bg_mat_idx * 64
        struct.pack_into(
            "<16f", buf, m_bg_off,
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            x0_left, 0.0, 0.0, 1.0
        )

    # Shift all remaining scanline placement matrices to tx = x0_left
    for mid in scanline_mats:
        struct.pack_into("<f", buf, t0_off + mid * 64 + 48, x0_left)

    # Expand full-screen fade quads (32x28 centered at 256,224 or 35x30 at -24,-16)
    for mid in fade_32_mats:
        struct.pack_into("<f", buf, t0_off + mid * 64, 32.0 * K)
    for mid in fade_35_mats:
        struct.pack_into("<f", buf, t0_off + mid * 64, 35.0 * K)
        struct.pack_into("<f", buf, t0_off + mid * 64 + 48, x0_left - 24.0)

    # Patch Type 0 bitmap quad records (512x448 backgrounds/frames and 16x128 scanlines)
    for di in range(t5_cnt):
        dtype, doff = dict_entries[di]
        if dtype == 0:
            cnt = struct.unpack_from("<i", buf, doff)[0]
            roff = doff + 4
            for _ in range(cnt):
                rtype, noff, midx, tex = struct.unpack_from("<4i", buf, roff)
                w, h = struct.unpack_from("<2f", buf, roff + 28)
                if abs(w - 512.0) < 1e-3 and abs(h - 448.0) < 1e-3:
                    if bg_mat_idx > 0 and midx == 0:
                        struct.pack_into("<i", buf, roff + 8, bg_mat_idx)
                        struct.pack_into("<f", buf, roff + 28, w_full)
                    else:
                        struct.pack_into("<f", buf, roff + 28, w_full)
                elif abs(w - 16.0) < 1e-3 and abs(h - 128.0) < 1e-3 and midx == 0:
                    # Scanline quad placed with m00 = 32.0
                    struct.pack_into("<f", buf, roff + 28, w_full / 32.0)
                elif menu_name == "MainSelect" and di == 13:
                    col1_scale = ((x0_left + 216.0 * K) - (22.5 + dx_left)) / 195.0
                    struct.pack_into("<f", buf, roff + 28, w * col1_scale)
                elif menu_name == "MainSelect" and di == 15:
                    col1_opt_scale = ((x0_left + 267.0 * K) - (22.5 + dx_left)) / 246.0
                    struct.pack_into("<f", buf, roff + 28, w * col1_opt_scale)
                elif menu_name == "MainSelect" and di == 17:
                    col2_opt_scale = ((485.0 + dx_right) - (x0_left + 267.5 * K)) / 217.5
                    struct.pack_into("<f", buf, roff + 28, w * col2_opt_scale)
                elif menu_name in ("AceStyle", "FM_Select") and di == 10:
                    col1_scale = ((x0_left + 216.0 * K) - (22.5 + dx_left)) / 195.0
                    struct.pack_into("<f", buf, roff + 28, w * col1_scale)
                elif menu_name in ("CampaignSelect", "MissionEnd") and di == 9:
                    col1_scale = ((x0_left + 216.0 * K) - (22.5 + dx_left)) / 195.0
                    struct.pack_into("<f", buf, roff + 28, w * col1_scale)
                roff = noff

    # =========================================================================
    # Menu-Specific UI Placement & ActionScript Patching:
    # =========================================================================
    if menu_name == "MainSelect" and t5_cnt in (272, 273):
        m4_off = t0_off + 4 * 64
        sx4 = struct.unpack_from("<f", buf, m4_off)[0]
        struct.pack_into("<f", buf, m4_off, sx4 * K)

        rel_mcs = [
            165, 166, 167, 185, 186, 187, 188, 189, 191, 192, 193, 194, 195, 196,
            206, 207, 210, 211, 214, 216, 217, 219, 220, 223, 227, 228, 229, 230,
            231, 232, 246, 247, 248, 249, 250, 257, 258
        ]
        abs_mcs = [172, 174, 197, 198, 199, 208, 212, 224, 233, 234, 235, 241, 251, 259, 260]

        s_rel = _collect_mc_mats(buf, dict_entries, rel_mcs)
        s_abs = (_collect_mc_mats(buf, dict_entries, abs_mcs) | {308, 309}) - s_rel

        left_dicts = {168, 169, 175, 176, 177, 178, 181, 182, 204, 238}
        right_dicts = {157, 160, 161}
        rel_dicts = {172, 198, 234, 241, 251, 260, 235}
        abs_dicts = {
            158, 165, 166, 167, 171, 173, 190, 191, 192, 193, 194,
            195, 196, 206, 207, 210, 211, 214, 216, 217, 219, 220, 223, 227,
            228, 229, 230, 231, 232, 244, 246, 247, 248, 249, 250, 257, 258
        }

        dtype, doff = dict_entries[263]
        _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
        m_left: Set[int] = set()
        m_right: Set[int] = set()
        for fi in range(frm_cnt):
            boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    _, _, child_d = struct.unpack_from("<3i", buf, poff)
                    mats = {_resolve_mat_idx(buf, p), _resolve_mat_idx(buf, poff)}
                    p += 16
                elif op in (4, 5):
                    _, _, child_d = struct.unpack_from("<3i", buf, p)
                    mats = {_resolve_mat_idx(buf, p)}
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                    continue
                elif op == 7:
                    p += 16
                    continue
                else:
                    p += 4
                    continue
                mats.discard(0)
                mats.discard(-1)
                if child_d in left_dicts:
                    m_left |= mats
                elif child_d in right_dicts:
                    m_right |= mats
                elif child_d in rel_dicts:
                    s_rel |= mats
                elif child_d in abs_dicts:
                    s_abs |= mats

        # Move Column 1 text item placements (tx < 150.0, e.g. tx=46.0) from s_abs to m_left
        col1_text_mats = {
            m for m in s_abs
            if struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0] < 150.0
        }
        s_abs -= col1_text_mats
        m_left |= col1_text_mats

        # Collect all matrices that place Dict[158] (text items) at Column 2 / Column 3
        text_item_mats: Set[int] = set()
        for di in range(t5_cnt):
            dt_i, do_i = dict_entries[di]
            if dt_i == 5:
                _, _, _, f_off_i, f_cnt_i = struct.unpack_from("<5i", buf, do_i)
                for fi_i in range(f_cnt_i):
                    bo_i, oc_i = struct.unpack_from("<ii", buf, f_off_i + fi_i * 8)
                    p_i = bo_i
                    for _ in range(oc_i):
                        op_i = struct.unpack_from("<i", buf, p_i)[0]
                        if op_i in (0, 1, 2, 3):
                            _, po_i, _, _ = struct.unpack_from("<4i", buf, p_i)
                            _, _, cd_i = struct.unpack_from("<3i", buf, po_i)
                            if cd_i == 158:
                                text_item_mats.add(_resolve_mat_idx(buf, p_i))
                                text_item_mats.add(_resolve_mat_idx(buf, po_i))
                            p_i += 16
                        elif op_i in (4, 5):
                            _, _, cd_i = struct.unpack_from("<3i", buf, p_i)
                            if cd_i == 158:
                                text_item_mats.add(_resolve_mat_idx(buf, p_i))
                            p_i += 20 if op_i == 4 else 24
                        elif op_i in (6, 8, 9):
                            p_i += 8
                        elif op_i == 7:
                            p_i += 16
                        else:
                            p_i += 4
        text_item_mats.discard(0)
        text_item_mats.discard(-1)

        for m in m_left:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_left)
        for m in m_right:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            # Dict[157] initial opening animation frames (tx < 460) scale from center; final scrollbar track frame (tx >= 460) shifts by dx_right
            if tx < 350.0 and m != 1544:
                struct.pack_into("<f", buf, moff + 48, 256.0 + (tx - 256.0) * K)
            else:
                struct.pack_into("<f", buf, moff + 48, tx + dx_right)
        for m in s_rel:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            new_tx = tx * K
            if tx < -230.0:
                # Column 1 rest position of bullet slide animation: lock to Column 1 left border (+dx_left)
                new_tx = tx * K + (dx_left - x0_left) - 37.45 * (K - 1.0)
            elif abs(tx - (-72.5)) < 1.0:
                # Column 2 rest position of bullet: preserve exact +5.95px offset from Divider 1
                new_tx = tx * K - 5.95 * (K - 1.0)
            elif abs(tx - 8.0) < 1.0:
                # Column 3 rest position of bullet: preserve exact +10.5px offset from Divider 2
                new_tx = tx * K - 10.5 * (K - 1.0)
            struct.pack_into("<f", buf, moff + 48, new_tx)

        s_abs -= s_rel
        for m in s_abs:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            new_tx = 256.0 + (tx - 256.0) * K
            if m in text_item_mats and tx > 220.0:
                anchor_gap = 17.3 if tx < 280.0 else 18.3
                new_tx -= anchor_gap * (K - 1.0)
            struct.pack_into("<f", buf, moff + 48, new_tx)

        # Align Column 2/3 OPTIONS highlight bar (Dict[183] / Shape[17], Mat[1613], opaque x_tex=3.0..220.5)
        # so its left edge sits on Divider 1 (x0_left + 267.5 * K) and right edge hits 485.0 + dx_right
        col2_opt_scale = ((485.0 + dx_right) - (x0_left + 267.5 * K)) / 217.5
        struct.pack_into("<f", buf, t0_off + 1613 * 64 + 48, (x0_left + 267.5 * K) - 3.0 * col2_opt_scale)

        if len(buf) >= 0xDED08:
            op_es, val_es = struct.unpack_from("<Ii", buf, 0xDECB4)
            op_de, val_de = struct.unpack_from("<Ii", buf, 0xDECCC)
            op_id, val_id = struct.unpack_from("<Ii", buf, 0xDECDC)
            if op_es == 7 and val_es == 84 and op_de == 7 and val_de == 99 and op_id == 7 and val_id == 169:
                struct.pack_into("<i", buf, 0xDECB8, 0)                         # ES -> use Mat[1586].tx
                struct.pack_into("<i", buf, 0xDECC0, 0)                         # IT -> use Mat[1586].tx
                struct.pack_into("<i", buf, 0xDECC8, 0)                         # FR -> use Mat[1586].tx
                struct.pack_into("<i", buf, 0xDECD0, int(round(99 + dx_left)))  # DE -> shifted X
                struct.pack_into("<i", buf, 0xDECD8, 0)                         # EN -> use Mat[1586].tx

        print(
            f"[*] Patched '{menu_name}' SWG to {aspect} ({target_width}x{target_height}): "
            f"m_left={len(m_left)}, m_right={len(m_right)}, s_rel={len(s_rel)}, s_abs={len(s_abs)}"
        )

    elif menu_name in ("AceStyle", "CampaignSelect", "FM_Select", "MissionEnd"):
        # 3-Column MainSelect-family sub-menus sharing entry_000_512x448 (9318AC217B572785)
        if menu_name == "AceStyle":
            uimc = 72
            rel_mcs = [42, 43, 49, 59, 61, 65, 66, 67]
            abs_mcs = [47, 50, 62, 63, 69, 70]
            left_dicts = {44, 45, 51, 52, 53, 56}
            rel_dicts = {47, 63, 70}
            abs_dicts = {42, 43, 46, 60, 61, 68}
            text_child_dict = 46
        elif menu_name == "CampaignSelect":
            uimc = 65
            rel_mcs = [41, 42, 43, 48, 58, 63]
            abs_mcs = [46, 49, 50, 60]
            left_dicts = {44, 51, 52, 53, 56, 59}
            rel_dicts = {49}
            abs_dicts = {41, 42, 43, 48, 58, 63}
            text_child_dict = 45
        elif menu_name == "FM_Select":
            uimc = 53
            rel_mcs = [32, 33, 39, 50]
            abs_mcs = [37, 40, 41, 51]
            left_dicts = {34, 35, 42, 43, 44, 47}
            rel_dicts = {40}
            abs_dicts = {32, 33, 39, 50}
            text_child_dict = 36
        else:  # MissionEnd
            uimc = 50
            rel_mcs = [36, 41]
            abs_mcs = [39, 42]
            left_dicts = {37, 43, 44, 45, 48}
            rel_dicts = {42}
            abs_dicts = {36}
            text_child_dict = 38

        s_rel = _collect_mc_mats(buf, dict_entries, rel_mcs)
        s_abs = _collect_mc_mats(buf, dict_entries, abs_mcs) - s_rel

        _, doff = dict_entries[uimc]
        _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
        m_left: Set[int] = set()
        for fi in range(frm_cnt):
            boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    _, _, child_d = struct.unpack_from("<3i", buf, poff)
                    mats = {_resolve_mat_idx(buf, p), _resolve_mat_idx(buf, poff)}
                    p += 16
                elif op in (4, 5):
                    _, _, child_d = struct.unpack_from("<3i", buf, p)
                    mats = {_resolve_mat_idx(buf, p)}
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                    continue
                elif op == 7:
                    p += 16
                    continue
                else:
                    p += 4
                    continue
                mats.discard(0)
                mats.discard(-1)
                if child_d in left_dicts:
                    m_left |= mats
                elif child_d in rel_dicts:
                    s_rel |= mats
                elif child_d in abs_dicts:
                    s_abs |= mats

        # Move Column 1 text item placements (tx < 150.0, e.g. tx=46.0) from s_abs to m_left
        col1_text_mats = {
            m for m in s_abs
            if struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0] < 150.0
        }
        s_abs -= col1_text_mats
        m_left |= col1_text_mats

        text_item_mats: Set[int] = set()
        for di in range(t5_cnt):
            dt_i, do_i = dict_entries[di]
            if dt_i == 5:
                _, _, _, f_off_i, f_cnt_i = struct.unpack_from("<5i", buf, do_i)
                for fi_i in range(f_cnt_i):
                    bo_i, oc_i = struct.unpack_from("<ii", buf, f_off_i + fi_i * 8)
                    p_i = bo_i
                    for _ in range(oc_i):
                        op_i = struct.unpack_from("<i", buf, p_i)[0]
                        if op_i in (0, 1, 2, 3):
                            _, po_i, _, _ = struct.unpack_from("<4i", buf, p_i)
                            _, _, cd_i = struct.unpack_from("<3i", buf, po_i)
                            if cd_i == text_child_dict:
                                text_item_mats.add(_resolve_mat_idx(buf, p_i))
                                text_item_mats.add(_resolve_mat_idx(buf, po_i))
                            p_i += 16
                        elif op_i in (4, 5):
                            _, _, cd_i = struct.unpack_from("<3i", buf, p_i)
                            if cd_i == text_child_dict:
                                text_item_mats.add(_resolve_mat_idx(buf, p_i))
                            p_i += 20 if op_i == 4 else 24
                        elif op_i in (6, 8, 9):
                            p_i += 8
                        elif op_i == 7:
                            p_i += 16
                        else:
                            p_i += 4
        text_item_mats.discard(0)
        text_item_mats.discard(-1)

        for m in m_left:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_left)
        for m in s_rel:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            new_tx = tx * K
            if tx < -230.0:
                new_tx = tx * K + (dx_left - x0_left) - 37.45 * (K - 1.0)
            elif abs(tx - (-72.5)) < 1.0:
                new_tx = tx * K - 5.95 * (K - 1.0)
            elif abs(tx - 8.0) < 1.0:
                new_tx = tx * K - 10.5 * (K - 1.0)
            struct.pack_into("<f", buf, moff + 48, new_tx)

        s_abs -= s_rel
        for m in s_abs:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            new_tx = 256.0 + (tx - 256.0) * K
            if m in text_item_mats and tx > 220.0:
                anchor_gap = 17.3 if tx < 280.0 else 18.3
                new_tx -= anchor_gap * (K - 1.0)
            struct.pack_into("<f", buf, moff + 48, new_tx)

        zz_patched = _patch_all_zz_setmovexy(buf, dx_left, dx_right)
        print(
            f"[*] Patched '{menu_name}' SWG to {aspect} ({target_width}x{target_height}): "
            f"m_left={len(m_left)}, s_rel={len(s_rel)}, s_abs={len(s_abs)}, zz_setMoveXY={zz_patched}"
        )

    elif menu_name in ("AircraftSelect", "AircraftSelect_Q", "Shop", "AircraftData", "DetailMap"):
        # Hangar / 3D Showcase / Map Menus (left HUD shifts by dx_left, right HUD shifts by dx_right)
        if menu_name in ("AircraftSelect", "Shop", "AircraftData"):
            # Shift dark grid internal centering matrix Mat[1].tx from -256.0 to -256.0 + x0_left
            struct.pack_into("<f", buf, t0_off + 1 * 64 + 48, -256.0 + x0_left)

        if menu_name == "AircraftSelect":
            uimc_list = [186]
            skip_children = {127, 128, 157, 158, 170, 172, 174, 177, 179, 180, 181, 183, 184, 185}
            btn_icon_children = {134, 135, 136, 182}
            m_left: Set[int] = {594, 595, 596, 597}
        elif menu_name == "AircraftSelect_Q":
            uimc_list = [167]
            skip_children = {114, 115, 124, 144, 145, 156, 158, 160, 162, 164, 165}
            btn_icon_children = {116, 117, 166}
            m_left = set()
        elif menu_name == "Shop":
            uimc_list = [152, 213]
            skip_children = {139, 151, 166, 193, 194, 205, 207, 209, 212}
            btn_icon_children = {141, 142}
            m_left = set()
        elif menu_name == "AircraftData":
            uimc_list = [43]
            skip_children = {20, 21, 42}
            btn_icon_children = {24}
            m_left = set()
        else:  # DetailMap
            uimc_list = [55]
            skip_children = {27, 45, 46, 47, 51, 52, 54}
            btn_icon_children = {48, 53}
            m_left = set()

        m_right: Set[int] = set()
        for uimc in uimc_list:
            _, doff = dict_entries[uimc]
            _, _, _, frm_off, frm_cnt = struct.unpack_from("<5i", buf, doff)
            for fi in range(frm_cnt):
                boff, opcnt = struct.unpack_from("<ii", buf, frm_off + fi * 8)
                p = boff
                for _ in range(opcnt):
                    op = struct.unpack_from("<i", buf, p)[0]
                    if op in (0, 1, 2, 3):
                        _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                        _, _, child_d = struct.unpack_from("<3i", buf, poff)
                        mats = {_resolve_mat_idx(buf, p), _resolve_mat_idx(buf, poff)}
                        p += 16
                    elif op in (4, 5):
                        _, _, child_d = struct.unpack_from("<3i", buf, p)
                        mats = {_resolve_mat_idx(buf, p)}
                        p += 20 if op == 4 else 24
                    elif op in (6, 8, 9):
                        p += 8
                        continue
                    elif op == 7:
                        p += 16
                        continue
                    else:
                        p += 4
                        continue

                    mats.discard(0)
                    mats.discard(-1)
                    if child_d in skip_children:
                        continue
                    if child_d in btn_icon_children:
                        m_left |= mats
                    else:
                        for m in mats:
                            tx = struct.unpack_from("<f", buf, t0_off + m * 64 + 48)[0]
                            if tx < 200.0:
                                m_left.add(m)
                            elif tx >= 300.0:
                                m_right.add(m)

        m_right -= m_left
        for m in m_left:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_left)
        for m in m_right:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        if menu_name == "DetailMap":
            strings_pool = _load_sym_pool(buf, 0xD8)
            for fn_name in ("briefing_setMapPosition", "briefing_setCoordinateXPosition", "briefing_setCoordinateYPosition"):
                if fn_name in strings_pool:
                    fsym = strings_pool.index(fn_name)
                    fsig = struct.pack("<5i", 8, 0, fsym, 77, 23)
                    fidx = buf.find(fsig)
                    if fidx >= 32:
                        op_x, val_x = struct.unpack_from("<Ii", buf, fidx - 32)
                        if op_x == 7:
                            struct.pack_into("<i", buf, fidx - 28, int(round(val_x + dx_right)))

        zz_patched = _patch_all_zz_setmovexy(buf, dx_left, dx_right)
        print(
            f"[*] Patched '{menu_name}' SWG to {aspect} ({target_width}x{target_height}): "
            f"m_left={len(m_left)}, m_right={len(m_right)}, zz_setMoveXY={zz_patched}"
        )

    elif menu_name in ("ControlSet", "LanguageSet", "VSSet"):
        # 2-Column Options Menus with vertical divider at x=356 (ControlSet/LanguageSet) or x=311 (VSSet)
        if menu_name in ("ControlSet", "LanguageSet"):
            uimc = 83
            left_children = {57, 58, 60, 64, 65, 66}
            right_children = {51, 52, 53, 54, 55, 56, 63, 72, 75, 77, 81}
            bar_mat = 220
            bar_base_w = 334.0
        else:  # VSSet
            uimc = 79
            left_children = {50, 51, 53, 59, 60, 61}
            right_children = {46, 47, 48, 49, 58, 69, 72, 77}
            bar_mat = 163
            bar_base_w = 289.0

        m_left: Set[int] = set()
        m_right: Set[int] = set()
        _, u_doff = dict_entries[uimc]
        _, _, _, u_foff, u_fcnt = struct.unpack_from("<5i", buf, u_doff)
        for fi in range(u_fcnt):
            boff, opcnt = struct.unpack_from("<ii", buf, u_foff + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    _, _, cd = struct.unpack_from("<3i", buf, poff)
                    mats = {_resolve_mat_idx(buf, p), _resolve_mat_idx(buf, poff)}
                    p += 16
                elif op in (4, 5):
                    _, _, cd = struct.unpack_from("<3i", buf, p)
                    mats = {_resolve_mat_idx(buf, p)}
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                    continue
                elif op == 7:
                    p += 16
                    continue
                else:
                    p += 4
                    continue
                mats.discard(0)
                mats.discard(-1)
                for m in mats:
                    sx = struct.unpack_from("<f", buf, t0_off + m * 64)[0]
                    if abs(sx - 1.0) > 0.05:
                        continue
                    if cd in left_children:
                        m_left.add(m)
                    elif cd in right_children:
                        m_right.add(m)

        m_right -= m_left
        for m in m_left:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_left)
        for m in m_right:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        # Stretch and shift left-column yellow highlight bar from left border to shifted vertical divider
        # compensating for the 3.5px transparent left margin inside entry_011_512x32
        b_off = t0_off + bar_mat * 64
        b_tx = struct.unpack_from("<f", buf, b_off + 48)[0]
        active_w = bar_base_w - 4.0
        scale_bar = (active_w + dx_right - dx_left) / active_w
        struct.pack_into("<f", buf, b_off, scale_bar)
        struct.pack_into("<f", buf, b_off + 48, b_tx + dx_left - 3.5 * (scale_bar - 1.0))

        zz_patched = _patch_all_zz_setmovexy(buf, dx_left, dx_right)
        print(
            f"[*] Patched '{menu_name}' SWG to {aspect} ({target_width}x{target_height}): "
            f"m_left={len(m_left)}, m_right={len(m_right)}, zz_setMoveXY={zz_patched}"
        )

    else:
        # Remaining 14 Standard Menus (AssaultRecord, Brightness, Calibration, CampResult,
        # FM_Mission, LanguageSet, Medals, MusicPlayer, SaveLoad, SceneViewer, ScrPosition, SoundSet,
        # SqData, Unlock, VSSet, VSstage):
        # Shift outer header/breadcrumb/sub-header/help-bar elements to widescreen edges while keeping
        # central tables, medal cases, pilot cards, sliders, and calibration crosshairs centered!
        t5_list = [i for i, (dt, _) in enumerate(dict_entries) if dt == 5]
        root_mc = t5_list[-1]
        _, r_doff = dict_entries[root_mc]
        _, _, _, r_foff, r_fcnt = struct.unpack_from("<5i", buf, r_doff)
        root_ch: Set[int] = set()
        for fi in range(r_fcnt):
            boff, opcnt = struct.unpack_from("<ii", buf, r_foff + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    _, _, cd = struct.unpack_from("<3i", buf, poff)
                    root_ch.add(cd)
                    p += 16
                elif op in (4, 5):
                    _, _, cd = struct.unpack_from("<3i", buf, p)
                    root_ch.add(cd)
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                elif op == 7:
                    p += 16
                else:
                    p += 4

        # Find the primary UI MovieClip
        uimc_candidates = []
        for c in root_ch:
            dt_c, do_c = dict_entries[c]
            if dt_c == 5:
                _, _, _, _, fc_c = struct.unpack_from("<5i", buf, do_c)
                if fc_c not in (1, 16, 22, 24, 180, 181):
                    uimc_candidates.append(c)
        # In AssaultRecord, Medals, MusicPlayer, there is a 60f 1-child intro wrapper and a 37f/50f main UI MC
        uimc = max(uimc_candidates, key=lambda c: c)

        m_left: Set[int] = set()
        m_right: Set[int] = set()
        _, u_doff = dict_entries[uimc]
        _, _, _, u_foff, u_fcnt = struct.unpack_from("<5i", buf, u_doff)
        for fi in range(u_fcnt):
            boff, opcnt = struct.unpack_from("<ii", buf, u_foff + fi * 8)
            p = boff
            for _ in range(opcnt):
                op = struct.unpack_from("<i", buf, p)[0]
                if op in (0, 1, 2, 3):
                    _, poff, _, _ = struct.unpack_from("<4i", buf, p)
                    _, _, cd = struct.unpack_from("<3i", buf, poff)
                    mats = {_resolve_mat_idx(buf, p), _resolve_mat_idx(buf, poff)}
                    p += 16
                elif op in (4, 5):
                    _, _, cd = struct.unpack_from("<3i", buf, p)
                    mats = {_resolve_mat_idx(buf, p)}
                    p += 20 if op == 4 else 24
                elif op in (6, 8, 9):
                    p += 8
                    continue
                elif op == 7:
                    p += 16
                    continue
                else:
                    p += 4
                    continue
                mats.discard(0)
                mats.discard(-1)
                for m in mats:
                    sx = struct.unpack_from("<f", buf, t0_off + m * 64)[0]
                    tx, ty = struct.unpack_from("<2f", buf, t0_off + m * 64 + 48)
                    if abs(sx - 1.0) > 0.05 or tx < 15.0:
                        continue
                    # Skip cur_focus_move (tx=16.5 or 22.5, ty=52.0 or 57.5)
                    if abs(tx - 22.5) < 0.1 or abs(tx - 16.5) < 0.1:
                        continue
                    if (10.0 <= ty <= 60.0 or ty >= 345.0) and tx < 260.0:
                        m_left.add(m)
                    elif menu_name == "MusicPlayer" and cd in (39, 40) and tx < 100.0:
                        m_left.add(m)
                    elif menu_name == "AssaultRecord" and tx >= 400.0 and (ty <= 60.0 or ty >= 390.0):
                        m_right.add(m)
                    elif menu_name == "Medals" and cd in (25, 26) and abs(tx - 46.0) < 1.0:
                        m_left.add(m)

        m_right -= m_left
        for m in m_left:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_left)
        for m in m_right:
            moff = t0_off + m * 64
            tx = struct.unpack_from("<f", buf, moff + 48)[0]
            struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        if menu_name == "SaveLoad":
            # 1) Stretch yellow file slot selection box (MC[34] M[3], wrapping Shape D[4] 480x96, active x=9..470)
            #    so at rest (M82..84 tx=295.0, M6..12 tx=-72.5) it spans 25.5+dx_left .. 486.5+dx_right
            scale_slot = (461.0 + dx_right - dx_left) / 461.0
            m3_off = t0_off + 3 * 64
            struct.pack_into("<f", buf, m3_off, scale_slot)
            struct.pack_into("<f", buf, m3_off + 48, -197.0 + dx_left + 189.0 * scale_slot)
            for m in list(range(13, 27)) + list(range(34, 48)):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, -72.5 + (tx - (-72.5)) * scale_slot)
            # 2) Shift left slot labels (tx=41.8: M[88..90, 118..120, 135..137]) and values (M[91..102, 105..116, 122..133]) by dx_left
            for m in (
                list(range(88, 103))
                + list(range(105, 117))
                + list(range(118, 121))
                + list(range(122, 134))
                + list(range(135, 138))
            ):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 3) Shift right slot [ACE STYLE] badges (M[103, 117, 134] at tx=407.5) by dx_right
            for m in (103, 117, 134):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        elif menu_name == "CampResult":
            # 1) Stretch bottom [ACE STYLE GAUGE] bar (M[2650, 2652] at tx=36.2, width 440px) across widescreen
            scale_gauge = (440.0 + dx_right - dx_left) / 440.0
            for m in (2650, 2652):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff, scale_gauge)
                struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Shift right column headers M[2660, 2661, 2662] (tx=354, 405, 456) by dx_right
            for m in (2660, 2661, 2662):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_right)
            # 3) Shift scrolling table rows M[100..2643] (Dict[57], tx=46.0) by dx_left,
            #    and shift right-side score/rank cells inside Dict[30, 43, 56] (M[33..44], M[75..86]) by (dx_right - dx_left)
            for m in range(100, 2644):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                if abs(tx - 46.0) < 1.0:
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            for m in list(range(33, 45)) + list(range(75, 87)):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + (dx_right - dx_left))
            # 4) Shift scrollbar track/thumb M[2644..2647] (tx=473.0) by dx_right
            for m in range(2644, 2648):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        elif menu_name == "AssaultRecord":
            # 1) Shift left pilot bio/portrait elements (M[63..70] at tx=46.0, M[77] at tx=127.0, M[78] at tx=151.0) by dx_left
            for m in list(range(63, 71)) + [77, 78]:
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Shift right scrollbar (M[54..58] at tx=467/473) and right labels (M[75, 76]) by dx_right without stretching width
            for m in (54, 56, 57, 58, 75, 76):
                if m not in m_right:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_right)
            # 3) Expand 5-column pilot grid (Dict[234] at M[59], tx=195.0; Dict[233] columns M[9..27]) across right pane
            scale_ar = (272.0 + dx_right - dx_left) / 272.0
            m59_off = t0_off + 59 * 64
            tx59 = struct.unpack_from("<f", buf, m59_off + 48)[0]
            struct.pack_into("<f", buf, m59_off + 48, tx59 + dx_left + 12.0 * (K - 1.0))
            for m in range(9, 28):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                if tx > 1.0:
                    struct.pack_into("<f", buf, moff + 48, tx * scale_ar)

        elif menu_name == "SqData":
            # 1) Shift left labels (tx=46.0: M[33..39, 77..81]) by dx_left
            for m in list(range(33, 40)) + list(range(77, 82)):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Spread the 3 pilot data columns (M[41..61, 83..94]) across widescreen width
            for m in list(range(41, 62)) + list(range(83, 95)):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, 256.0 + (tx - 256.0) * K)
            # 3) Stretch bottom [ACE STYLE GAUGE] bar (M[82] at tx=36.2, width 440px) across widescreen
            scale_gauge = (440.0 + dx_right - dx_left) / 440.0
            m82_off = t0_off + 82 * 64
            tx82 = struct.unpack_from("<f", buf, m82_off + 48)[0]
            struct.pack_into("<f", buf, m82_off, scale_gauge)
            struct.pack_into("<f", buf, m82_off + 48, tx82 + dx_left)

        elif menu_name == "Medals":
            # 1) Shift large medal preview (M[50], tx=31.5) and [AWARD TITLE] (M[44, 45], tx=46.0) by dx_left
            for m in (44, 45, 50):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Shift 7-column medal grid root (M[42], tx=164.0) by dx_left and spread 7 columns (M[8..27]) across right pane
            scale_medals = (321.0 + dx_right - dx_left) / 321.0
            m42_off = t0_off + 42 * 64
            tx42 = struct.unpack_from("<f", buf, m42_off + 48)[0]
            struct.pack_into("<f", buf, m42_off + 48, tx42 + dx_left + 12.0 * (K - 1.0))
            for m in range(8, 28):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                if tx > 1.0:
                    struct.pack_into("<f", buf, moff + 48, tx * scale_medals)

        elif menu_name == "SceneViewer":
            # 1) Shift left scene info container D77 (M[71] at tx=0.0) by dx_left
            for m in (71, 72, 73, 74, 75):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Shift right preview border box D58 (M[48,50,52,54,56,58,60,62,64,65] at tx=392..265)
            #    AND cutscene preview image D76 (M[70] at tx=314.5) together by dx_right
            for m in (48, 50, 52, 54, 56, 58, 60, 62, 64, 65, 70):
                if m not in m_right:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_right)
            # 3) Spread bottom scene selector strip columns (M[3..35], tx=16.0..429.5) across widescreen
            #    while keeping parent matrices M[68, 69] at 1:1 scale (sx=1.0) so digits & boxes stay centered
            for m in (68, 69):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            scale_cols = (413.5 + dx_right - dx_left) / 413.5
            for m in range(3, 36):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, 16.0 + (tx - 16.0) * scale_cols)

        elif menu_name == "FM_Mission":
            # 1) Shift left mission info & score containers D70/D85 (M[167] at tx=0.0) by dx_left
            for m in (167, 168, 169, 170, 171):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Shift right preview border box D50 (M[144,146,148,150,152,154,156,158,160,161] at tx=392..265)
            #    AND mission preview image D69 (M[166] at tx=314.5) together by dx_right
            for m in (144, 146, 148, 150, 152, 154, 156, 158, 160, 161, 166):
                if m not in m_right:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_right)
            # 3) Spread bottom mission selector strip columns across widescreen while keeping
            #    parent matrices M[164, 165, 172..175] at 1:1 scale (sx=1.0) so digits & ranks stay centered
            for m in (164, 165, 172, 173, 174, 175):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            scale_cols = (419.5 + dx_right - dx_left) / 419.5
            for m in list(range(2, 57)) + [113]:
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, 16.0 + (tx - 16.0) * scale_cols)
            for m in range(58, 113):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, 16.0 + ((tx - 12.0) - 16.0) * scale_cols + 12.0)

        elif menu_name == "MusicPlayer":
            # 1) Shift top track info (M[68, 69] at tx=46.0) by dx_left
            for m in (68, 69):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            # 2) Expand 3-column track list (Dict[33] at M[59], tx=164.0; columns M[9..50] at tx=-125.0, 28.5, 182.0)
            #    so all 3 columns fit strictly inside the outer frame borders [26.5 + dx_left .. 485.5 + dx_right]
            scale_mp = (459.0 + dx_right - dx_left) / 459.0
            m59_off = t0_off + 59 * 64
            struct.pack_into("<f", buf, m59_off + 48, 26.5 + dx_left + (164.0 - 26.5) * scale_mp)
            for m in range(9, 51):
                moff = t0_off + m * 64
                tx = struct.unpack_from("<f", buf, moff + 48)[0]
                struct.pack_into("<f", buf, moff + 48, tx * scale_mp)
            # 4) Stretch yellow track highlight bar + glow container D30 (placed at M[7] inside MC[32]) across full column width,
            #    while keeping track title text D31 (placed at M[8] inside MC[32]) unstretched and aligned with left column margin
            m7_off = t0_off + 7 * 64
            tx7 = struct.unpack_from("<f", buf, m7_off + 48)[0]
            struct.pack_into("<f", buf, m7_off, scale_mp)
            struct.pack_into("<f", buf, m7_off + 48, tx7 * scale_mp)
            m8_off = t0_off + 8 * 64
            tx8 = struct.unpack_from("<f", buf, m8_off + 48)[0]
            struct.pack_into("<f", buf, m8_off + 48, tx8 - 13.0 * (scale_mp - 1.0))

        elif menu_name == "Unlock":
            # Shift unlocked aircraft/feature announcement text lines (M[10, 11, 13, 14, 15, 16, 17] at tx=46.0) by dx_left
            for m in (10, 11, 13, 14, 15, 16, 17):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)

        elif menu_name == "VSstage":
            # Shift left stage info (M[51, 52, 54..64]) by dx_left and right stage preview box + image (M[31,33..48, 53]) by dx_right
            for m in (51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64):
                if m not in m_left:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_left)
            for m in (31, 33, 35, 37, 39, 41, 43, 45, 47, 48, 53):
                if m not in m_right:
                    moff = t0_off + m * 64
                    tx = struct.unpack_from("<f", buf, moff + 48)[0]
                    struct.pack_into("<f", buf, moff + 48, tx + dx_right)

        zz_patched = _patch_all_zz_setmovexy(buf, dx_left, dx_right)
        print(
            f"[*] Patched '{menu_name}' SWG to {aspect} ({target_width}x{target_height}): "
            f"m_left={len(m_left)}, m_right={len(m_right)}, zz_setMoveXY={zz_patched}"
        )

    return bytes(buf)


def patch_swg_to_16_9(swg_bytes: bytes, target_width: int = 682, target_height: int = 448) -> bytes:
    aspect = "21x9" if target_width >= 800 else "16x9"
    return patch_swg_to_widescreen(swg_bytes, aspect=aspect)


# Shared 512x448 background hashes that are stretched full-screen and do not need 9-slice border preservation
_FULLSCREEN_BG_HASHES = {
    "36D1C62CFB36194F",
    "D3B7396D70904454",
    "D1BA74F5B9E1CE37",
    "72646A07B01FE42C",
    "273F6934B121537C",
    "5DAE1E3459D19822",
    "8A01BDE921920821",
    "7C8891B62D176A1F",
    "AB7D8630A0D54600",
    "F830CC419E95C8E9",
    "F88085F5EFD56C74",
    "00291F28E14534B3",
}

# 3-column MainSelect-family frame hashes that already have custom _16x9/_21x9 in MainSelect
_MAINSELECT_SHARED_FRAME_HASHES = {
    "9318AC217B572785",
    "E8E5F50B71F1AE00",
    "68D30EE8B53CBB84",
}

# Tiled dark grid hash (AircraftSelect[009], AircraftData[006], Shop[008])
_DARK_GRID_HASH = "45D3B3A44FCBC859"


def _generate_menu_png_json_assets(input_swa_path: str, out_dir: str, stem: str):
    """
    Exports entry_NNN_*.png + entry_NNN_*.json via ACZ_TIM2_Scanner
    and builds 9-slice _16x9.png (684x448) and _21x9.png (896x448) widescreen textures
    for all 512x448 outer frames and grids.
    """
    try:
        import tempfile
        from PIL import Image
        import numpy as np
        import ACZ_TIM2_Scanner

        with tempfile.TemporaryDirectory() as tmp_scan:
            ACZ_TIM2_Scanner.run_tim2_scanner(
                input_swa_path, tmp_scan, by_container=True, save_raw_tm2=False
            )
            _PAD_GLYPH_HASHES = {
                "7A254AB048031FBD", "731D243962401485", "CFB7637AE69876CF",
                "A2A24050B802AC46", "AA306350C3C82BBE", "0168C928045DFE53",
                "003F893C639F06CA", "994E30D1236D7F42", "604875945C8B00CD",
                "83B36903776584A0", "F62A7A788E5A75B8", "AAB6349CD23095C9",
                "D1652FC7B015CCFE", "901EAE0AA4A02596", "BBB9F4E59FCD67A7",
                "E873026673283428", "1E73661762301D90", "EDF4FB8E2B2D1C59",
                "0CB7D1D48AA2F6B8", "33FB2583F10E0BA0", "033EE9A81DD6AC2E",
                "8F752D57784A8DC4", "BCACEF530991831C", "FF9AC43A9B523C52",
            }
            cont_dir = os.path.join(tmp_scan, "by_container", stem)
            if os.path.isdir(cont_dir):
                for f in glob.glob(os.path.join(cont_dir, "*.png")) + glob.glob(os.path.join(cont_dir, "*.json")):
                    bn_up = os.path.basename(f).upper()
                    if any(ph in bn_up for ph in _PAD_GLYPH_HASHES):
                        continue
                    dst_f = os.path.join(out_dir, os.path.basename(f))
                    if not os.path.exists(dst_f):
                        shutil.copy2(f, dst_f)

        # Generate _16x9.png (684x448) and _21x9.png (896x448) for 512x448 frame/grid textures
        for png_path in sorted(glob.glob(os.path.join(out_dir, "entry_*_512x448_*.png"))):
            bn = os.path.basename(png_path)
            if "_16x9" in bn or "_21x9" in bn:
                continue
            tex_hash = bn.split("_")[-1].split(".")[0].upper()
            if tex_hash in _FULLSCREEN_BG_HASHES:
                from PIL import ImageOps
                im_src = Image.open(png_path).convert("RGBA")
                arr = np.array(im_src)
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    offset_x = (target_w - 512) // 2
                    right_w = target_w - 512 - offset_x
                    if tex_hash == "7C8891B62D176A1F":
                        # WakeLogo Bandai Namco logo on solid white background
                        out = np.full((448, target_w, 4), 255, dtype=np.uint8)
                        out[:, offset_x:offset_x + 512, :] = arr
                        Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
                    elif tex_hash == "D3B7396D70904454":
                        # 2D square red grid with smooth dark radial vignette
                        arr_f = arr.astype(np.float32)
                        pad = 13
                        padded = np.pad(arr_f, ((0, 0), (pad, pad), (0, 0)), mode="reflect")
                        low = np.zeros_like(arr_f)
                        for dx in range(2 * pad + 1):
                            low += padded[:, dx:dx + 512, :]
                        low /= (2 * pad + 1)
                        high = arr_f - low
                        out_f = np.zeros((448, target_w, 4), dtype=np.float32)
                        out_f[:, offset_x:offset_x + 512, :] = arr_f
                        for x in range(offset_x):
                            atten = 0.85 + 0.15 * (x / max(1, offset_x))
                            out_f[:, x, :3] = low[:, 0, :3] * atten + high[:, offset_x - x, :3]
                            out_f[:, x, 3] = 255.0
                        for x in range(right_w):
                            atten = 1.0 - 0.15 * (x / max(1, right_w))
                            out_f[:, offset_x + 512 + x, :3] = low[:, 511, :3] * atten + high[:, 510 - x, :3]
                            out_f[:, offset_x + 512 + x, 3] = 255.0
                        Image.fromarray(np.clip(out_f, 0, 255).astype(np.uint8)).save(png_path.replace(".png", f"_{suffix}.png"))
                    elif tex_hash == "F830CC419E95C8E9":
                        # VSstage frame
                        out = np.zeros((448, target_w, 4), dtype=np.uint8)
                        out[:, 8:40, :] = arr[:, 0:32, :]
                        out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                        out[:, 40:offset_x + 32, :] = arr[:, 60:61, :]
                        out[:, offset_x + 480:target_w - 40, :] = arr[:, 450:451, :]
                        out[:, offset_x + 32:offset_x + 480, :] = arr[:, 32:480, :]
                        Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
                    else:
                        # Photographic / cloud / spark / vignette backgrounds: aspect-preserving 1:1 Lanczos crop-fill
                        fitted = ImageOps.fit(im_src, (target_w, 448), method=Image.Resampling.LANCZOS)
                        fitted.save(png_path.replace(".png", f"_{suffix}.png"))
                continue
            if tex_hash in _MAINSELECT_SHARED_FRAME_HASHES:
                # 3-column MainSelect-family frame (9318AC217B572785, E8E5F50B71F1AE00, 68D30EE8B53CBB84):
                # Outer left/right borders at 8..40 (x=33..34) and target_w-40..target_w-8 (target_w-35..target_w-34)
                # matching 25+dx_left and 485+dx_right, with crisp 2px interior dividers at int(round(x_div * K))!
                arr = np.array(Image.open(png_path).convert("RGBA"))
                div_cols = [216, 350] if tex_hash == "9318AC217B572785" else ([267, 375] if tex_hash == "E8E5F50B71F1AE00" else [267])
                for target_w, suffix, k_val in ((684, "16x9", 4.0 / 3.0), (896, "21x9", 1.75)):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 40:target_w - 40, :] = arr[:, 60:61, :]
                    for dc in div_cols:
                        dst_x = int(round(dc * k_val))
                        out[:, dst_x:dst_x + 2, :] = arr[:, dc:dc + 2, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
                continue

            arr = np.array(Image.open(png_path).convert("RGBA"))
            if tex_hash == _DARK_GRID_HASH:
                # Tile 192-px interior pattern cleanly across 684 and 896
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    for x in range(target_w):
                        out[:, x, :] = arr[:, 64 + (x % 192), :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash == "75CD912F7DCDA6EA":
                # AircraftSelect_Q[009]: 9-slice outer frame on y=0..205 & 342..448,
                # plus centered 6-column aircraft grid box on y=205..342 with outer vertical border
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    offset_x = (target_w - 512) // 2
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 40:offset_x + 32, :] = arr[:, 40:41, :]
                    out[:, offset_x + 480:target_w - 40, :] = arr[:, 471:472, :]
                    out[:, offset_x + 32:offset_x + 480, :] = arr[:, 32:480, :]
                    # Preserve full 6-column grid (x=24..488) centered on y=205..342
                    out[205:342, 40:target_w - 40, :] = 0
                    out[205:342, 8:40, :] = arr[100:101, 0:32, :]
                    out[205:342, target_w - 40:target_w - 8, :] = arr[100:101, 480:512, :]
                    out[205:342, offset_x + 24:offset_x + 488, :] = arr[205:342, 24:488, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash in ("B0BF303CE5574A88", "5739AD6E26719D7C"):
                # ControlSet/LanguageSet (B0BF303CE5574A88) & VSSet (5739AD6E26719D7C):
                # Shift left border (0..256) to 8..264 and right pane + vertical divider (256..512) to target_w-264..target_w-8
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:264, :] = arr[:, 0:256, :]
                    out[:, target_w - 264:target_w - 8, :] = arr[:, 256:512, :]
                    out[:, 264:target_w - 264, :] = arr[:, 256:257, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash == "18BD08968C3C252F":
                # ScrPosition[004]: outer border at 0..45 / 467..512, diagonal crosshair box at 45..467
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    offset_x = (target_w - 512) // 2
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:53, :] = arr[:, 0:45, :]
                    out[:, target_w - 53:target_w - 8, :] = arr[:, 467:512, :]
                    out[:, 53:offset_x + 45, :] = arr[:, 45:46, :]
                    out[:, offset_x + 467:target_w - 53, :] = arr[:, 466:467, :]
                    out[:, offset_x + 45:offset_x + 467, :] = arr[:, 45:467, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash == "B832BE08D234028B":
                # CampResult[000]: top table (y=0..298) has right columns at x=332..486 -> shift to right edge;
                # bottom [ACE STYLE GAUGE] (y=298..342) stretched proportionally across full widescreen width
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 196:target_w - 8, :] = arr[:, 324:512, :]
                    out[:, 40:target_w - 196, :] = arr[:, 60:61, :]
                    w_dst = target_w - 66
                    for x_dst in range(w_dst):
                        x_src = 25 + int(x_dst * 460 / w_dst)
                        out[298:342, 33 + x_dst, :] = arr[298:342, x_src, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash in ("59F3E39BA2897484", "1D9E648A436EA524"):
                # Medals[006] (divider at x=158..159) & AssaultRecord[008] (divider at x=190..191):
                # Shift left pane + vertical divider (0..200) to 8..208 and right border (480..512) to target_w-40..target_w-8
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:208, :] = arr[:, 0:200, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 208:target_w - 40, :] = arr[:, 250:251, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash == "FA70D7B452627913":
                # MusicPlayer[003]: preserve outer frame borders and place crisp 2px vertical column dividers at 1/3 and 2/3
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 40:target_w - 40, :] = arr[:, 60:61, :]
                    d1 = int(round(33.5 + (target_w - 67) / 3.0))
                    d2 = int(round(33.5 + 2.0 * (target_w - 67) / 3.0))
                    out[:, d1:d1 + 2, :] = arr[:, 178:180, :]
                    out[:, d2:d2 + 2, :] = arr[:, 332:334, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            elif tex_hash == "DF552FE2B22C50E9":
                # SceneViewer[003] / FM_Mission[003]: extend outer frame and horizontal divider lines cleanly across full width
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 40:target_w - 40, :] = arr[:, 60:61, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
            else:
                # Universal interior-preserving 9-slice widescreen frame:
                # - Left outer border (0..32) -> 8..40
                # - Right outer border (480..512) -> target_w-40..target_w-8
                # - Horizontal bars extended via x=60 / x=450 (avoids x=40..41 gap in SaveLoad)
                # - Interior table/divider lines (32..480) kept centered at offset_x+32..offset_x+480
                for target_w, suffix in ((684, "16x9"), (896, "21x9")):
                    offset_x = (target_w - 512) // 2
                    out = np.zeros((448, target_w, 4), dtype=np.uint8)
                    out[:, 8:40, :] = arr[:, 0:32, :]
                    out[:, target_w - 40:target_w - 8, :] = arr[:, 480:512, :]
                    out[:, 40:offset_x + 32, :] = arr[:, 60:61, :]
                    out[:, offset_x + 480:target_w - 40, :] = arr[:, 450:451, :]
                    out[:, offset_x + 32:offset_x + 480, :] = arr[:, 32:480, :]
                    Image.fromarray(out).save(png_path.replace(".png", f"_{suffix}.png"))
    except Exception as exc:
        print(f"[!] Warning: Could not auto-generate PNG+JSON textures for {stem}: {exc}")


def make_widescreen_variants(input_swa_path: str, out_dir: str):
    """
    Builds widescreen menu overrides in out_dir:
      - <Stem>.swa (clean 4:3 original) & chunk00_layout.swg (clean 4:3)
      - <Stem>_16x9.swa & chunk00_layout_16x9.swg
      - <Stem>_21x9.swa & chunk00_layout_21x9.swg
      - entry_NNN_*.png + entry_NNN_*.json (+ _16x9.png / _21x9.png variants)
      - Removes temporary chunkNN_texture.tm2 and manifest.json files.
    """
    unpack_swad(input_swa_path, out_dir)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as mf:
        manifest = json.load(mf)

    stem = os.path.splitext(os.path.basename(input_swa_path))[0]
    raw_chunks = []
    for c in manifest["chunks"]:
        with open(os.path.join(out_dir, c["filename"]), "rb") as cf:
            raw_chunks.append(cf.read())

    # Upgrade MainSelect 4:3 SWG to 273-entry (4:3 / 16:9 / 21:9) if needed
    if stem == "MainSelect":
        upgraded_43 = _upgrade_mainselect_to_3opt(raw_chunks[0])
        if upgraded_43 != raw_chunks[0]:
            raw_chunks[0] = upgraded_43
            with open(os.path.join(out_dir, "chunk00_layout.swg"), "wb") as f:
                f.write(raw_chunks[0])

    # Copy or repack 4:3 .swa
    dst_43 = os.path.join(out_dir, f"{stem}.swa")
    if stem == "MainSelect":
        repack_swad_from_buffers(raw_chunks, dst_43)
    elif os.path.abspath(input_swa_path) != os.path.abspath(dst_43):
        shutil.copyfile(input_swa_path, dst_43)

    for aspect in ("16x9", "21x9"):
        ws_swg = patch_swg_to_widescreen(raw_chunks[0], aspect=aspect)
        ws_chunks = [ws_swg] + raw_chunks[1:]
        ws_swa_path = os.path.join(out_dir, f"{stem}_{aspect}.swa")
        repack_swad_from_buffers(ws_chunks, ws_swa_path)
        print(f"[+] Built {aspect} menu override: {ws_swa_path}")

    # Remove temporary .swg, .tm2, .bin, and manifest.json files from menu directory
    for tmp_file in (glob.glob(os.path.join(out_dir, "*.swg")) +
                     glob.glob(os.path.join(out_dir, "*.tm2")) +
                     glob.glob(os.path.join(out_dir, "*.bin"))):
        os.remove(tmp_file)
    if os.path.exists(manifest_path):
        os.remove(manifest_path)

    # Ensure entry_NNN_*.png and .json exist in out_dir
    _generate_menu_png_json_assets(dst_43, out_dir, stem)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ACZ SWAD (.swa) Converter & Widescreen Tool")
    parser.add_argument("mode", choices=["unpack", "repack", "widescreen", "make-widescreen"], help="Operation mode")
    parser.add_argument("input", help="Input file or manifest")
    parser.add_argument("output", help="Output file or directory")
    parser.add_argument("--aspect", choices=["16x9", "21x9"], default="16x9", help="Target aspect ratio")
    parser.add_argument("--width", type=int, default=682, help="Target canvas width (682 for 16:9, 896 for 21:9)")
    args = parser.parse_args()

    if args.mode == "unpack":
        unpack_swad(args.input, args.output)
    elif args.mode == "repack":
        repack_swad(args.input, args.output)
    elif args.mode == "make-widescreen":
        make_widescreen_variants(args.input, args.output)
    elif args.mode == "widescreen":
        tmp_unpacked = "/tmp/swad_patch_tmp"
        unpack_swad(args.input, tmp_unpacked)
        swg_path = os.path.join(tmp_unpacked, "chunk00_layout.swg")
        with open(swg_path, "rb") as f:
            swg_d = f.read()
        aspect = "21x9" if (args.aspect == "21x9" or args.width >= 800) else "16x9"
        swg_patched = patch_swg_to_widescreen(swg_d, aspect=aspect)
        with open(swg_path, "wb") as f:
            f.write(swg_patched)
        repack_swad(os.path.join(tmp_unpacked, "manifest.json"), args.output)
