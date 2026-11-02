@echo off
rem = """
setlocal EnableExtensions
chcp 65001 >nul 2>nul

set "BAT_DIR=%~dp0"
set "PY_EXE="

rem Check local or portable Python
if exist "%BAT_DIR%py\python\python.exe" (
    set "PY_EXE=%BAT_DIR%py\python\python.exe"
    goto :run_py
)
if exist "%BAT_DIR%..\py\python\python.exe" (
    set "PY_EXE=%BAT_DIR%..\py\python\python.exe"
    goto :run_py
)
if exist "%BAT_DIR%python\python.exe" (
    set "PY_EXE=%BAT_DIR%python\python.exe"
    goto :run_py
)
if exist "%TEMP%\ACZero_PyPortable\python.exe" (
    set "PY_EXE=%TEMP%\ACZero_PyPortable\python.exe"
    goto :run_py
)

rem Check system Python
where python >nul 2>nul
if not errorlevel 1 (
    python -c "import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)" >nul 2>nul
    if not errorlevel 1 (
        set "PY_EXE=python"
        goto :run_py
    )
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)" >nul 2>nul
    if not errorlevel 1 (
        set "PY_EXE=py -3"
        goto :run_py
    )
)

rem Bootstrap portable python via PowerShell
echo [*] Python 3 not found in PATH.
echo [*] Setting up Zero-Install Portable Python in TEMP...
set "PORTABLE_DIR=%TEMP%\ACZero_PyPortable"
if not exist "%PORTABLE_DIR%" mkdir "%PORTABLE_DIR%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; if (-not (Test-Path (Join-Path $env:PORTABLE_DIR 'python.exe'))) { Write-Host '[*] Downloading Portable Python 3.11...'; $z = Join-Path $env:TEMP 'py_embed.zip'; Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.11.9/python-3.11.9-embed-amd64.zip' -OutFile $z -UseBasicParsing; Expand-Archive -Path $z -DestinationPath $env:PORTABLE_DIR -Force; Remove-Item $z -Force -ErrorAction SilentlyContinue; $pth = Join-Path $env:PORTABLE_DIR 'python311._pth'; if (Test-Path $pth) { Add-Content -Path $pth -Value ('Lib\site-packages' + [char]13 + [char]10 + 'site-packages' + [char]13 + [char]10 + 'import site') } }; if (-not (Test-Path (Join-Path $env:PORTABLE_DIR 'Lib\site-packages\numpy')) -and -not (Test-Path (Join-Path $env:PORTABLE_DIR 'site-packages\numpy'))) { Write-Host '[*] Installing pip, Pillow, and NumPy into portable Python...'; $gp = Join-Path $env:TEMP 'get-pip.py'; Invoke-WebRequest -Uri 'https://bootstrap.pypa.io/get-pip.py' -OutFile $gp -UseBasicParsing; & (Join-Path $env:PORTABLE_DIR 'python.exe') $gp --no-warn-script-location; Remove-Item $gp -Force -ErrorAction SilentlyContinue; & (Join-Path $env:PORTABLE_DIR 'python.exe') -m pip install pillow numpy --no-warn-script-location }"

if exist "%PORTABLE_DIR%\python.exe" (
    set "PY_EXE=%PORTABLE_DIR%\python.exe"
    goto :run_py
)

echo [ERROR] Python 3 was not found in PATH and portable setup failed!
echo Please install Python 3.10+ (or place embeddable python in a 'py\python' folder).
pause
exit /b 1

:run_py
%PY_EXE% -W ignore -x "%~f0" %*
set "RET_CODE=%ERRORLEVEL%"

if "%RET_CODE%"=="99" (
    del /f /q "%~f0" >nul 2>nul
    exit /b 0
)

if "%~1"=="" pause
exit /b %RET_CODE%
"""
# -*- coding: utf-8 -*-
r"""
Ace Combat Zero: The Belkan War (PC Port) — ACZero Enhancer Texture Pack Installer & Converter (v0.21)
======================================================================================================
Zero-install portable .bat + Python installer and converter for:
  - "actrilogy-hud-upgrade-v1.2.1.zip" (by OstrichMods)
  - "custom textures.zip" (containing "For OstrichMods\" sources and ready-to-use "textures\" replacements)
Extracts the Ace Combat Zero subfolder (SLUS-21346/replacements/hud-upgrade/) and converts all HUD,
Aircraft Icon, Weapon Icon, Squadron Emblem, Master Font Atlas, and Hangar Weapon Atlas textures into
ready-to-use replacements for acz_hdtex.dll (v0.21) without requiring manual Python/Pillow/NumPy setup.
"""

import base64
import glob
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

def _ensure_imaging_libs():
    # 1. Add all potential site-packages locations to sys.path first
    pkg_dir = os.path.join(tempfile.gettempdir(), "ACZero_PyPortable", "site-packages")
    pkg_lib_dir = os.path.join(tempfile.gettempdir(), "ACZero_PyPortable", "Lib", "site-packages")
    local_pkg = os.path.join(os.path.dirname(os.path.abspath(__file__)), "site-packages")
    prefix_pkg = os.path.join(sys.prefix, "Lib", "site-packages")
    prefix_pkg2 = os.path.join(sys.prefix, "site-packages")

    for p in (prefix_pkg, prefix_pkg2, pkg_lib_dir, pkg_dir, local_pkg):
        if p and p not in sys.path:
            sys.path.insert(0, p)

    # 2. Try importing
    try:
        import numpy as _np
        from PIL import Image as _Image
        return _np, _Image
    except ImportError:
        pass

    # 3. Try pip install if pip is available
    print("[*] Installing Pillow and NumPy (required for image processing)...")
    try:
        res = subprocess.run([sys.executable, "-m", "pip", "install", "pillow", "numpy", "--quiet"], check=False)
        if res.returncode == 0:
            import numpy as _np
            from PIL import Image as _Image
            return _np, _Image
    except Exception:
        pass

    # 4. If pip is missing (e.g. in embeddable Python), bootstrap pip via get-pip.py
    try:
        import urllib.request
        print("[*] Setting up pip in portable Python environment...")
        gp = os.path.join(tempfile.gettempdir(), "get-pip.py")
        urllib.request.urlretrieve("https://bootstrap.pypa.io/get-pip.py", gp)
        subprocess.run([sys.executable, gp, "--no-warn-script-location", "--quiet"], check=False)
        try:
            os.remove(gp)
        except OSError:
            pass

        # Now install with pip
        subprocess.run([sys.executable, "-m", "pip", "install", "pillow", "numpy", "--no-warn-script-location", "--quiet"], check=False)
        import numpy as _np
        from PIL import Image as _Image
        return _np, _Image
    except Exception as e:
        print(f"[*] Pip bootstrap notice: {e}")

    # 5. Final attempt
    try:
        import numpy as _np
        from PIL import Image as _Image
        return _np, _Image
    except ImportError as exc:
        print(f"[ERROR] Missing required library ({exc}). Please install: pip install pillow numpy")
        sys.exit(1)

np, Image = _ensure_imaging_libs()

PLUGIN_VERSION = "v0.21 (ACZero Enhancer)"
ACZ_SUBPATH_PREFIX = "SLUS-21346/replacements/hud-upgrade/"

AIRCRAFT_MAP = {'1288ce8874c60963-368bd523e0f6c443-00001993.png': 'm29a_06006AE1983ADAA8.png',
 '138bd4c1f9cc0f92-ecf5b59336262e0a-00001993.png': 'x29a_B4DA063F189618C4.png',
 '26eba0adbdc237d1-da57d1d1a3645e7b-00001993.png': 'f18c_C5DE7270894B083C.png',
 '29c8bf1af5eaaca7-720b91ff46125795-00001993.png': 'j35j_844E8A9173AD1EF2.png',
 '2a9ae8d823db8856-24dc26b36a36b55e-00001993.png': 'f35c_E0CFFDE52E113900.png',
 '2ac683e94bb8d69f-68010effa2250d89-00001993.png': 'f14d_493CE5B4A368E52E.png',
 '2ed0628ef5c5f2ca-4bfc82babd4540f7-00001993.png': 'f02a_C181058F4EFED7F8.png',
 '34bfa5e2c876c316-1004bc381d1a869f-00001993.png': 'yf23_2A2D86BDF02106F2.png',
 '351647316ecb6d64-5bd84e7f96122b53-00001993.png': 'f04e_D39CE5C09778032B.png',
 '3d93f340a6af4540-e1ff5002ec08d1e0-00001993.png': 'tnd4_2C0EFEA58FFAB12B.png',
 '45d969db397e064b-bcafb7dfe1dc9398-00001993.png': 'f05e_4042036B7974E846.png',
 '4fa26665bcf39a14-e1561577c3e2a825-00001993.png': 'f16x_7C25D563486E64F0.png',
 '543f338d224438cc-8bfcd06204445049-00001993.png': 'su37_2CA861E869083593.png',
 '544d335a6f49436-367d0570f6914b9f-00001993.png': 'zoef_D6E7F13AB1C907BF.png',
 '54dc48153c816353-756bc4920a8178b2-00001993.png': 'su47_3FFA84155DA91E6C.png',
 '5908e6e3de3f3a00-26fd9b7c5889893d-00001993.png': 'rm01_D5F2452A114BA0B7.png',
 '6800b078a8b5af0-f193c4ad1f3f5d93-00001993.png': 'typn_F7452D45ADC556B5.png',
 '68796a474ce4e8c1-ecfb78559f378c2b-00001993.png': 'm31b_D6D9DFE81F98F859.png',
 '6c54f580ef2c4909-d6599b7ebe3fba5d-00001993.png': 'f15c_f15e_7BBFF633CB7C5D32.png',
 '7ed8165e70cd983f-27fbb65e8d1655b9-00001993.png': 'f20a_8DFB806B65E17A75.png',
 '8062d239cf15ac74-63cbf9105c886d0d-00001993.png': 'a10a_94E4A1201D2DC4F7.png',
 '87947be35d0e1c31-a29feaeca6368809-00001993.png': 'su27_BCA8414D46EE4590.png',
 '8882a0396570e80e-d4be15cda19a7749-00001993.png': 'f01a_B7861A3597D47450.png',
 'a31c0ac694fba0fb-acfe46803a7fc89-00001993.png': 'e06b_D97A62F77D93DE32.png',
 'a9afba494bb556eb-2e8b63c688418d6c-00001993.png': 'x02a_F483B6C8062A3438.png',
 'c2aa95994888d956-aaee28ad980286ee-00001993.png': 'su32_5A89454A61AC7B11.png',
 'cb24d3d2130e8b92-867f9ec62cdb9ee1-00001993.png': 'mrgn_4579101E74130E99.png',
 'd55b92924a8ea6d1-f37e34a68ce0d77-00001993.png': 'e18g_13FFFE0E93CAAF0F.png',
 'd8988ddf070f7ec0-6cf3573b51a9544-00001993.png': 'j39c_EC2787173F88E073.png',
 'db8454eb14bf12e-5a7d1f6122f4b304-00001993.png': 'f17a_9DD7B050C73AC981.png',
 'df6c5267f5f0be48-8bfcd06204445049-00001993.png': 'f15s_AFE0689A762F2083.png',
 'e865dc677f7db382-c9229a3f3a295ce9-00001993.png': 'mr2d_0573278134C82C4A.png',
 'f636491f116b2123-fdc9334e10db845a-00001993.png': 'm21b_BDF5F8A29A0C2AA9.png',
 'f912a981aae5453-9e6c9b646577f5c0-00001993.png': 'fa22_1C180245B9B88A9D.png',
 'fa27a1dd6aea1ebb-83cb11ec8c0290af-00001993.png': 'f16c_7735868F5A08C140.png'}

WEAPONS_MAP = {'17780a775a06417f-5b395e0806a29787-00001553.png': ['f01a_spw3_F9BEB845CAFF3998.png'],
 '197ae439eef759e1-b364924c70000301-00001553.png': ['m31b_spw3_2D16EB50177EFF62.png'],
 '1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png': ['su37_spw3_91DF45EA2ACEE24B.png'],
 '1cf7f6ad376f713e-62852b858c81242b-00001553.png': ['mrgn_spw2_8405864A2557042D.png'],
 '1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png': ['f02a_spw2_2ACDA5CBF8F320B8.png'],
 '2722168c224dda31-fb2acc81acbc589-00001553.png': ['m21b_spw3_059AB79A15989B94.png'],
 '294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png': ['fa22_spw3_265ED844345F9BA1.png'],
 '2c5a883bac7f9d52-d769d53355989eb2-00001553.png': ['typn_spw2_0CFE653F7AB700D4.png',
                                                    'rm01_spw3_6C1E590336883DB9.png'],
 '2e8bde22462e97cd-f869a1d3cec69f20-00001553.png': ['m21b_spw2_A59779CD4569253C.png'],
 '3073f6813e500d9a-f09687360676a0f2-00001553.png': ['mrgn_spw3_66DA5E58D032BD0F.png'],
 '350eb8028387d459-de67f70996c01abf-00001553.png': ['su32_spw2_785945B3568585FA.png'],
 '38131cfd253bd46b-47ed4644be84356f-00001553.png': ['j35j_spw1_F7A20D49BE7A4FCF.png'],
 '38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png': ['m29a_spw1_1EA3646FD215458F.png'],
 '3e388ad275889e1a-48d7e64a0958b001-00001553.png': ['a10a_spw1_9865E33639C60CC1.png'],
 '3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png': ['yf23_spw3_D563851A328A5897.png'],
 '411cc74c13352163-fe926e6c6d66aba3-00001553.png': ['mr2d_spw2_2D6A61F2E1841277.png'],
 '43ef3bb8935a13d6-a797282f96013320-00001553.png': ['f35c_spw2_287E5EAB7C5D9716.png'],
 '4664458b39154bf4-d16bf124bac220ff-00001553.png': ['yf23_spw2_9B95EDCCF6E02C75.png'],
 '46e6afb636674472-b527b56ababf8da1-00001553.png': ['su27_spw1_8231540C8D788ADD.png'],
 '52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png': ['tnd4_spw2_451C7256CC93CB18.png'],
 '52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png': ['su27_spw3_1964C4F3EB456382.png'],
 '596cbdf2619f7b8a-962148d78acbe33a-00001553.png': ['m29a_spw2_6536B9D7DBF4E705.png'],
 '5ecc5c786debc32c-26e26f6886e0d34-00001553.png': ['e06b_spw1_E51501FE1B4644C0.png'],
 '5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png': ['a10a_spw3_69EAC6AB9A0588FD.png'],
 '6674fc448194ba9a-482a2ed3d52a2c59-00001553.png': ['m31b_spw1_282B6C1A039AB32A.png'],
 '6e5420ff947c1f25-f3c43724228eef6e-00001553.png': ['m21b_spw1_155A61C08974143C.png'],
 '76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png': ['m29a_spw3_20EA2B44089F5CE4.png'],
 '78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png': ['tnd4_spw3_C124F336D322FD47.png'],
 '8448b79b04c936f4-13673bbe6030c235-00001553.png': ['j35j_spw3_FC6A8CACE59E6E54.png'],
 '8bcd96efda99fe7a-fed5690acbeed124-00001553.png': ['zoef_spw1_FB9FC3E82648A216.png'],
 '92c47ba79a448999-f8e1829d3e674ad7-00001553.png': ['m21b_spw0_99A0E0769FB20D4C.png'],
 '9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png': ['j39c_spw1_2D475397AB600845.png'],
 '9af02ab15cd6defb-54fe8338087b28e3-00001553.png': ['f14d_spw1_6A01A49942644FBA.png'],
 '9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png': ['su37_spw2_51FB5F5BA4049DC3.png'],
 '9dcc4303ca58d4ae-3255aab68abec63f-00001553.png': ['f01a_spw1_786626BB30FBEECF.png'],
 'a0a76953d9ad8745-79745ac7a081d802-00001553.png': ['f15e_spw3_5FC34E95B6BEDA87.png'],
 'a3fe94a35e094a5b-b31023f17d2742cd-00001553.png': ['rm01_spw1_3F6AB5615D675E51.png'],
 'a935373d012b23f9-bd68f36a3bb0ae08-00001553.png': ['f35c_spw1_0E64EB127C763646.png'],
 'ae3a10251b90c162-e03e4c7c5afa510a-00001553.png': ['f04e_spw1_657335F95C306B91.png'],
 'b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png': ['f15s_spw2_3C2985F3CACD20AD.png'],
 'b3cd052625961d0e-ab103eeda9f5b27a-00001553.png': ['rm01_spw2_22EAB05B69C3C16F.png'],
 'b95d58c0d63aea15-9ab5acb80064b414-00001553.png': ['f14d_spw2_BFB2431B72BD265D.png'],
 'ba57f120b72bdc35-912159579341a858-00001553.png': ['x02a_spw3_A944834B961E5D05.png'],
 'bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png': ['mr2d_spw3_28CF209F3A16809E.png'],
 'bd9ecdc337510310-74f330c2a5f1c999-00001553.png': ['f18c_spw1_DD67FBF0F65ED935.png'],
 'c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png': ['f16x_spw3_1E9434CB14021D0E.png'],
 'c343a8decadc17f5-8cc6e211a14a059e-00001553.png': ['f04e_spw3_E1867FC688451E97.png'],
 'c55dd3c30e462850-4300ae6334756736-00001553.png': ['f02a_spw3_18D4853B7EB2023B.png'],
 'cdfe64556a6bbab-9aded927d43c386e-00001553.png': ['mr2d_spw1_66A5E3FDD718D511.png'],
 'd2379ebef5fc21e2-92e8d3a22733591b-00001553.png': ['e06b_spw2_BDA1B5BA948B4559.png'],
 'd6253421a2720ba3-ee47d64d1554ab8c-00001553.png': ['f15s_spw3_141AC19E776E787B.png'],
 'd66b021c903d74a0-35db2265694342c2-00001553.png': ['mrgn_spw1_4F6CE8FB1DB22F79.png'],
 'd8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png': ['f04e_spw2_9C3976886EC05F9D.png'],
 'db3b21eea2d6c00c-603e736c5ccb05ce-00001553.png': ['a10a_spw0_A42AC6E2DE7D1468.png'],
 'db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png': ['a10a_spw2_06B8610C5FC49B95.png'],
 'ddfecded193d0052-6c56475f596994d6-00001553.png': ['f01a_spw0_7FA9F929A6E0DC9A.png'],
 'dfe97d3b32645f46-7cea02778833eddf-00001553.png': ['su47_spw3_20D368E1179065DF.png'],
 'e129b2f0cd36758f-e896e0119e3ea71b-00001553.png': ['mr2d_spw0_D9A680AB5D9773E7.png'],
 'e37af4f2dca452d2-ca27388b97df0ca-00001553.png': ['j39c_spw3_5CED8F2FA45170F5.png'],
 'ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png': ['e18g_spw2_90034118EAF4A9BE.png'],
 'f222b31462e369bb-4ca89a758f76b82a-00001553.png': ['x02a_spw1_ADE75846BB1D29C9.png'],
 'f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png': ['typn_spw3_CC6654C55861F733.png'],
 'f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png': ['e06b_spw3_93D30AABDE2721FF.png']}

EMBLEMS_MAP = {'1b5dd1b386e329be-93694d338453595e-00001dd3.png': ('m10nmark_A87DFCDC3658B5A7.png',
                                                    (112, 32, 400, 480),
                                                    (76, 52, 436, 456)),
 '24951acba7b316b7-2baab0f72a36dd1-00001dd3.png': ('m01amark_CBBEC4E1F6384A5C.png',
                                                   (129, 37, 385, 477),
                                                   (96, 52, 416, 456)),
 '264f26884df4c1f8-49cb05e0e21ff1b3-00001dd3.png': ('m16mmark_AD3E175DEFFC645C.png',
                                                    (110, 32, 402, 480),
                                                    (76, 52, 436, 456)),
 '3d5453c19cc2a93a-ff54e576f50d601a-00001dd3.png': ('m16kmark_14677E40C87757D4.png',
                                                    (110, 32, 402, 480),
                                                    (76, 52, 436, 456)),
 '5040703048016a26-d0f929d58cf2ea30-00001dd3.png': ('m03nmark_655407D78D147291.png',
                                                    (112, 33, 399, 479),
                                                    (76, 52, 436, 456)),
 '72f6b4fb5bacde00-2c81f95a13987bf6-00001dd3.png': ('m06amark_CB733A27052F1DB8.png',
                                                    (113, 32, 400, 479),
                                                    (76, 52, 436, 456)),
 '90addf4e0f67d523-ff4c8a3d1e4d904c-00001dd3.png': ('m03kmark_27B1F74C766F1F88.png',
                                                    (136, 32, 376, 480),
                                                    (108, 52, 404, 456)),
 '9775b289551fcd85-be3fc70bd883d7e0-00001dd3.png': ('m10kmark_9349DBBDFEB4B9C3.png',
                                                    (140, 34, 372, 478),
                                                    (112, 56, 400, 456)),
 'a2b807a07f471713-65b19129de2451d8-00001dd3.png': ('m15amark_80B29295644904B5.png',
                                                    (111, 35, 402, 477),
                                                    (72, 52, 440, 456)),
 'c788e68c3f731500-9b219a3c671080d2-00001dd3.png': ('m10mmark_6125F057A6FFA729.png',
                                                    (120, 32, 392, 480),
                                                    (88, 52, 424, 456)),
 'e3ca12d8500c3bfa-5b5f08da9ed05911-00001dd3.png': ('m03mmark_1C5B6F181DEED6DF.png',
                                                    (112, 32, 400, 480),
                                                    (76, 52, 436, 456)),
 'e974d059dfff0d0b-d802700cc88a0777-00001dd3.png': ('m16nmark_92E5F129F09C32AB.png',
                                                    (140, 27, 371, 487),
                                                    (112, 48, 400, 460))}



DESC_CLUSTERS_MAP = {0: ('3fdf48525eaa6f4f-50d9bb4f3b4c31e3-00001dd3.png', (32, 8, 424, 253), (120, 8, 520, 376)),
 1: ('f3ac2ec3e4517d24-f528ad9877ea26c8-00001dd3.png', (32, 18, 425, 246), (120, 56, 528, 328)),
 2: ('b624060ac4312c73-3befbaa31c78d496-00001dd3.png', (32, 8, 424, 245), (120, 40, 528, 344)),
 3: ('3b9e2c9e3f0f4a50-88405d1baecd3218-00001dd3.png', (35, 5, 417, 256), (120, 40, 512, 344)),
 4: ('56ea817ac2db1374-c2800969a031abdd-00001dd3.png', (32, 20, 424, 250), (120, 32, 528, 360)),
 5: ('b201ed0c5be98d03-55742ccae9576cb3-00001dd3.png', (31, 16, 425, 247), (96, 56, 544, 328)),
 6: ('46189e7714a2db61-924071ca6c3f4072-00001dd3.png', (34, 15, 424, 253), (104, 40, 520, 336)),
 7: ('dd5777addc054c1c-b49d07d1f81930d0-00001dd3.png', (31, 29, 425, 231), (120, 72, 520, 312)),
 8: ('af900764cef49b5d-4f69e21e4ef073e7-00001dd3.png', (32, 21, 424, 239), (120, 56, 528, 336)),
 9: ('5136522b0bf64466-2a75b0a8a7190c07-00001dd3.png', (32, 7, 424, 253), (112, 24, 520, 360)),
 10: ('b2415975b7392277-b9334aa2fd29456f-00001dd3.png', (30, 27, 429, 240), (104, 64, 520, 320)),
 11: ('dbb0932edca59176-3c8a187d4603fbc1-00001dd3.png', (31, 24, 425, 234), (120, 64, 520, 344)),
 12: ('e002c7dceffb07b0-8976b2ae0db32e49-00001dd3.png', (31, 29, 425, 223), (120, 72, 528, 320)),
 13: ('201e77548a217d7c-72fd80ce708287f5-00001dd3.png', (37, 12, 424, 248), (104, 40, 520, 336)),
 14: ('fe157270c7eb0a86-e1e5e0e89eab2811-00001dd3.png', (32, 33, 424, 234), (112, 64, 520, 328)),
 15: ('4e7a43c50cdbc6c6-1321231175ff6b07-00001dd3.png', (31, 23, 425, 243), (112, 56, 528, 328)),
 16: ('fe0edc2cb47edc7f-bbb7f2d2715f41b2-00001dd3.png', (32, 36, 425, 222), (120, 64, 520, 320)),
 17: ('27bd9581d4cda07f-a9554d40e9f0acec-00001dd3.png', (29, 106, 427, 153), (104, 160, 536, 224)),
 18: ('mpbm_desc.png', (95, 56, 417, 246), (120, 72, 520, 312))}

HANGAR_ATLAS_MAP = {'a10a_F0D04BDF1D284DA1.png': [('3e388ad275889e1a-48d7e64a0958b001-00001553.png', 0),
                               ('db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png', 1),
                               ('5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png', 2)],
 'e06b_8EA988653D8C2B9B.png': [('5ecc5c786debc32c-26e26f6886e0d34-00001553.png', 3),
                               ('d2379ebef5fc21e2-92e8d3a22733591b-00001553.png', 4),
                               ('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5)],
 'e18g_CDD4B97B06CA1B3B.png': [('5ecc5c786debc32c-26e26f6886e0d34-00001553.png', 3),
                               ('ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png', 6),
                               ('d2379ebef5fc21e2-92e8d3a22733591b-00001553.png', 4)],
 'f01a_1D5BCC417A3B9845.png': [('9dcc4303ca58d4ae-3255aab68abec63f-00001553.png', 2),
                               ('3B97DB2F5EB61035.png', 7),
                               ('17780a775a06417f-5b395e0806a29787-00001553.png', 8)],
 'f02a_3DCBB3CF2C9DF9B1.png': [('17780a775a06417f-5b395e0806a29787-00001553.png', 8),
                               ('1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png', 6),
                               ('c55dd3c30e462850-4300ae6334756736-00001553.png', 9)],
 'f04e_19A37B37E6057469.png': [('ae3a10251b90c162-e03e4c7c5afa510a-00001553.png', 10),
                               ('d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png', 11),
                               ('c343a8decadc17f5-8cc6e211a14a059e-00001553.png', 12)],
 'f05e_4D452C25FBB55E58.png': [('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5),
                               ('3B97DB2F5EB61035.png', 7),
                               ('c343a8decadc17f5-8cc6e211a14a059e-00001553.png', 12)],
 'f14d_90003464704FBB86.png': [('9af02ab15cd6defb-54fe8338087b28e3-00001553.png', 13),
                               ('b95d58c0d63aea15-9ab5acb80064b414-00001553.png', 14),
                               ('c343a8decadc17f5-8cc6e211a14a059e-00001553.png', 12)],
 'f15c_658A8D9540582856.png': [('ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png', 6),
                               ('d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png', 11),
                               ('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5)],
 'f15e_9F0018B9C800CD7D.png': [('c55dd3c30e462850-4300ae6334756736-00001553.png', 9),
                               ('ae3a10251b90c162-e03e4c7c5afa510a-00001553.png', 10),
                               ('a0a76953d9ad8745-79745ac7a081d802-00001553.png', 14)],
 'f15s_A698D6196509B9A8.png': [('db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png', 1),
                               ('b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png', 13),
                               ('d6253421a2720ba3-ee47d64d1554ab8c-00001553.png', 15)],
 'f16c_61117992C3458826.png': [('ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png', 6),
                               ('d2379ebef5fc21e2-92e8d3a22733591b-00001553.png', 4),
                               ('d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png', 11)],
 'f16x_6350E2CCC30517F7.png': [('3e388ad275889e1a-48d7e64a0958b001-00001553.png', 0),
                               ('ae3a10251b90c162-e03e4c7c5afa510a-00001553.png', 10),
                               ('c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png', 16)],
 'f17a_AC15E0D83C11AE33.png': [('a0a76953d9ad8745-79745ac7a081d802-00001553.png', 14),
                               ('d2379ebef5fc21e2-92e8d3a22733591b-00001553.png', 4),
                               ('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5)],
 'f18c_720A744FB814C7EF.png': [('bd9ecdc337510310-74f330c2a5f1c999-00001553.png', 8),
                               ('ae3a10251b90c162-e03e4c7c5afa510a-00001553.png', 10),
                               ('d6253421a2720ba3-ee47d64d1554ab8c-00001553.png', 15)],
 'f20a_EB4A6AFAA377B1E0.png': [('ae3a10251b90c162-e03e4c7c5afa510a-00001553.png', 10),
                               ('b95d58c0d63aea15-9ab5acb80064b414-00001553.png', 14),
                               ('3e388ad275889e1a-48d7e64a0958b001-00001553.png', 0)],
 'f35c_E2EE94E7578243C4.png': [('a935373d012b23f9-bd68f36a3bb0ae08-00001553.png', 8),
                               ('43ef3bb8935a13d6-a797282f96013320-00001553.png', 15),
                               ('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5)],
 'fa22_178DD0DF96594614.png': [('ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png', 6),
                               ('b95d58c0d63aea15-9ab5acb80064b414-00001553.png', 14),
                               ('294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png', 15)],
 'j35j_648E8EB6A9BEA04A.png': [('38131cfd253bd46b-47ed4644be84356f-00001553.png', 10),
                               ('3B97DB2F5EB61035.png', 7),
                               ('8448b79b04c936f4-13673bbe6030c235-00001553.png', 2)],
 'j39c_CF186F0203B1496A.png': [('9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png', 2),
                               ('b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png', 13),
                               ('e37af4f2dca452d2-ca27388b97df0ca-00001553.png', 15)],
 'm21b_AD17BB01F7808901.png': [('6e5420ff947c1f25-f3c43724228eef6e-00001553.png', 2),
                               ('2e8bde22462e97cd-f869a1d3cec69f20-00001553.png', 7),
                               ('2722168c224dda31-fb2acc81acbc589-00001553.png', 5)],
 'm29a_9AC6CA2D0F6C3021.png': [('38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png', 10),
                               ('596cbdf2619f7b8a-962148d78acbe33a-00001553.png', 2),
                               ('76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png', 0)],
 'm31b_900E239BBBFECCFA.png': [('6674fc448194ba9a-482a2ed3d52a2c59-00001553.png', 13),
                               ('2722168c224dda31-fb2acc81acbc589-00001553.png', 5),
                               ('197ae439eef759e1-b364924c70000301-00001553.png', 11)],
 'mr2d_8ECF99F3F694C9EC.png': [('cdfe64556a6bbab-9aded927d43c386e-00001553.png', 7),
                               ('411cc74c13352163-fe926e6c6d66aba3-00001553.png', 14),
                               ('bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png', 0)],
 'mrgn_9AC691BF97A302BB.png': [('d66b021c903d74a0-35db2265694342c2-00001553.png', 17),
                               ('1cf7f6ad376f713e-62852b858c81242b-00001553.png', 18),
                               ('3073f6813e500d9a-f09687360676a0f2-00001553.png', 3)],
 'rm01_6FCFCCA91CDF4384.png': [('a3fe94a35e094a5b-b31023f17d2742cd-00001553.png', 8),
                               ('b3cd052625961d0e-ab103eeda9f5b27a-00001553.png', 6),
                               ('2c5a883bac7f9d52-d769d53355989eb2-00001553.png', 15)],
 'su27_E65621C11C263479.png': [('46e6afb636674472-b527b56ababf8da1-00001553.png', 6),
                               ('197ae439eef759e1-b364924c70000301-00001553.png', 11),
                               ('52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png', 9)],
 'su32_603E1220C1EB8029.png': [('76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png', 0),
                               ('350eb8028387d459-de67f70996c01abf-00001553.png', 4),
                               ('46e6afb636674472-b527b56ababf8da1-00001553.png', 6)],
 'su37_B340E3E70F82D83B.png': [('6674fc448194ba9a-482a2ed3d52a2c59-00001553.png', 13),
                               ('9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png', 1),
                               ('1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png', 12)],
 'su47_84EB97CB90263929.png': [('38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png', 10),
                               ('52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png', 9),
                               ('dfe97d3b32645f46-7cea02778833eddf-00001553.png', 16)],
 'tnd4_0A84CA287C1A466A.png': [('c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png', 16),
                               ('52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png', 3),
                               ('78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png', 8)],
 'typn_11887AFD1050D2B7.png': [('b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png', 13),
                               ('2c5a883bac7f9d52-d769d53355989eb2-00001553.png', 15),
                               ('f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png', 9)],
 'x02a_194FE72DF3296614.png': [('f222b31462e369bb-4ca89a758f76b82a-00001553.png', 13),
                               ('2722168c224dda31-fb2acc81acbc589-00001553.png', 5),
                               ('ba57f120b72bdc35-912159579341a858-00001553.png', 0)],
 'x29a_33E8B4E5880EBF11.png': [('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5),
                               ('db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png', 1),
                               ('d2379ebef5fc21e2-92e8d3a22733591b-00001553.png', 4)],
 'yf23_DB5CE3CDD410E988.png': [('f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png', 5),
                               ('4664458b39154bf4-d16bf124bac220ff-00001553.png', 12),
                               ('3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png', 16)],
 'zoef_376441BBEC9C1F58.png': [('8bcd96efda99fe7a-fed5690acbeed124-00001553.png', 17),
                               ('b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png', 13),
                               ('9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png', 1)]}

HANGAR_TIM2_WEAP_SPECS = [{'dst_320': (112, 16, 208, 304),
  'hashes': ['CFA2D375FCC1FFE4', '56A435935CFA3E7C', '147EFE7C51BBF4F6'],
  'out_name': 'hangar_weap_f05e_spw1_CFA2D375FCC1FFE4.png',
  'weap_fn': 'f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['EE6126699C475707', '969C41F65599E35F', 'C79117E9AF81B3C1'],
  'out_name': 'hangar_weap_j35j_spw1_EE6126699C475707.png',
  'weap_fn': '38131cfd253bd46b-47ed4644be84356f-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['5DDDD2026D66F07A', 'BC742359A0130922', 'C95CEEB1640CA36B'],
  'out_name': 'hangar_weap_f01a_spw1_5DDDD2026D66F07A.png',
  'weap_fn': '9dcc4303ca58d4ae-3255aab68abec63f-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['35DBC22B9136D977', 'AC80EF2109A98BCF', '3DBA0E66EBE6130D'],
  'out_name': 'hangar_weap_m21b_spw1_35DBC22B9136D977.png',
  'weap_fn': '6e5420ff947c1f25-f3c43724228eef6e-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['2E319165C0962F14', '45ADF68B55DD89DC', '5D6C68397F3EB8B7'],
  'out_name': 'hangar_weap_f04e_spw1_2E319165C0962F14.png',
  'weap_fn': 'ae3a10251b90c162-e03e4c7c5afa510a-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['A0C4D4FBC888E036', '4EA2434B8302738E', '42E59A8A6DE258CD'],
  'out_name': 'hangar_weap_m29a_spw1_A0C4D4FBC888E036.png',
  'weap_fn': '38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['7B315A095AA43355', 'EAB957B48EEC155D', 'D8F14846602E2C82'],
  'out_name': 'hangar_weap_f16c_spw1_7B315A095AA43355.png',
  'weap_fn': 'ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['C20413F3356A0AFF', 'E09B3ADEE2E2D8A7', '3310FA543A77C1D1'],
  'out_name': 'hangar_weap_f18c_spw1_C20413F3356A0AFF.png',
  'weap_fn': 'bd9ecdc337510310-74f330c2a5f1c999-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['98B1F48B6820C303', '62F591C4BE2001EB', '3C6CE62679ED7EB9'],
  'out_name': 'hangar_weap_a10a_spw1_98B1F48B6820C303.png',
  'weap_fn': '3e388ad275889e1a-48d7e64a0958b001-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['088C8AF8F77197B3', '96B07118015C2C6B', 'BA393723E6B57056'],
  'out_name': 'hangar_weap_m31b_spw1_088C8AF8F77197B3.png',
  'weap_fn': '6674fc448194ba9a-482a2ed3d52a2c59-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['85BED3A9CB88801C', 'F2C13336B34777B4', '9DFDFAB66A20A9F4'],
  'out_name': 'hangar_weap_mr2d_spw1_85BED3A9CB88801C.png',
  'weap_fn': 'cdfe64556a6bbab-9aded927d43c386e-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['BE57249B51C426FC', 'E0B99FFB4428FCF4', '93FA363E5E5677E3'],
  'out_name': 'hangar_weap_e06b_spw1_BE57249B51C426FC.png',
  'weap_fn': '5ecc5c786debc32c-26e26f6886e0d34-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['2CDCB303780BA5F8', '0820F2E113386860', '521769DC4B26061B'],
  'out_name': 'hangar_weap_su27_spw1_2CDCB303780BA5F8.png',
  'weap_fn': '46e6afb636674472-b527b56ababf8da1-00001553.png'},
 {'dst_320': (120, 16, 216, 304),
  'hashes': ['8CD5C5514D756229', '4DCE095EF19EA3D1', '10149C4CB54F975F'],
  'out_name': 'hangar_weap_f14d_spw1_8CD5C5514D756229.png',
  'weap_fn': '9af02ab15cd6defb-54fe8338087b28e3-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['D9E5E34B8DCB86D2', '21EFE923760D3C5A', 'F6174021CCCDFB01'],
  'out_name': 'hangar_weap_j39c_spw1_D9E5E34B8DCB86D2.png',
  'weap_fn': '9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['69A39F228699DE1F', '45FC7AE18CC4EF77', '96642B4034E7A9B5'],
  'out_name': 'hangar_weap_tnd4_spw1_69A39F228699DE1F.png',
  'weap_fn': 'c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['2BFF9C7ED749DC96', 'AA2552C9F60D35DE', '44F34DEC80ECCF3C'],
  'out_name': 'hangar_weap_f02a_spw1_2BFF9C7ED749DC96.png',
  'weap_fn': '17780a775a06417f-5b395e0806a29787-00001553.png'},
 {'dst_320': (128, 16, 200, 304),
  'hashes': ['1FA936B0074347A7', 'A4ADB515D279D7BF', '32D25826501254F4'],
  'out_name': 'hangar_weap_f15e_spw1_1FA936B0074347A7.png',
  'weap_fn': 'c55dd3c30e462850-4300ae6334756736-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['D2FE457BE9B50F03', '0D0AE0B78A610F9B', '678779E977B35DB7'],
  'out_name': 'hangar_weap_f17a_spw1_D2FE457BE9B50F03.png',
  'weap_fn': 'a0a76953d9ad8745-79745ac7a081d802-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['137398EF6F8F329F', 'DC705C7B6C797577', 'EF76DEE56720C9B2'],
  'out_name': 'hangar_weap_f35c_spw1_137398EF6F8F329F.png',
  'weap_fn': 'a935373d012b23f9-bd68f36a3bb0ae08-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['9445E413F263712E', 'FBE70A8275B20A86', '901398266F701965'],
  'out_name': 'hangar_weap_rm01_spw1_9445E413F263712E.png',
  'weap_fn': 'a3fe94a35e094a5b-b31023f17d2742cd-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['4F80CE9F9E847061', 'C29623495D7E5A29', 'DBB65A4F93B1F234'],
  'out_name': 'hangar_weap_typn_spw1_4F80CE9F9E847061.png',
  'weap_fn': 'b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['E8501C293E0566DD', 'AF6864E4537F2BC5', '3C92CF3EE2561BE2'],
  'out_name': 'hangar_weap_su32_spw1_E8501C293E0566DD.png',
  'weap_fn': '76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['DFFDB7362B0AAAE5', '8A2B27DABC7F65BD', '77D0B3ED141A4163'],
  'out_name': 'hangar_weap_f15s_spw1_DFFDB7362B0AAAE5.png',
  'weap_fn': 'db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['2001CB5B29CF0380', '9458FCEF3C0D4F58', '2C1718B70F7A5E76'],
  'out_name': 'hangar_weap_x02a_spw1_2001CB5B29CF0380.png',
  'weap_fn': 'f222b31462e369bb-4ca89a758f76b82a-00001553.png'},
 {'dst_320': (96, 16, 224, 304),
  'hashes': ['8A84AB341FD81E0B', 'A512C686F83E99F3', 'C714DAE82029D74C'],
  'out_name': 'hangar_weap_mrgn_spw1_8A84AB341FD81E0B.png',
  'weap_fn': 'd66b021c903d74a0-35db2265694342c2-00001553.png'},
 {'dst_320': (96, 16, 224, 304),
  'hashes': ['7904A659A587F072', '3BCC612F2D0BF41A', '2422EE4B45B26CEB'],
  'out_name': 'hangar_weap_zoef_spw1_7904A659A587F072.png',
  'weap_fn': '8bcd96efda99fe7a-fed5690acbeed124-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['19897FD506CDFDCB', 'B047DA2E2EDC82B3', '9A470186A34E4020'],
  'out_name': 'hangar_weap_f05e_spw2_19897FD506CDFDCB.png',
  'weap_fn': '3B97DB2F5EB61035.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['2151C0A66C452010', '48396377334C9AC8', '2CECD1533AB50DBC'],
  'out_name': 'hangar_weap_m21b_spw2_2151C0A66C452010.png',
  'weap_fn': '2e8bde22462e97cd-f869a1d3cec69f20-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['944C68908A79DDBB', 'F431FBA2515D2D43', '60957694A3C78467'],
  'out_name': 'hangar_weap_f04e_spw2_944C68908A79DDBB.png',
  'weap_fn': 'd8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['E4CAF508147F3B2A', '9B0D8E9C77F75C72', '9B8285C925790337'],
  'out_name': 'hangar_weap_m29a_spw2_E4CAF508147F3B2A.png',
  'weap_fn': '596cbdf2619f7b8a-962148d78acbe33a-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['401510E242FD8149', 'B7B0FAFA5FC91341', 'E9EC017C0F990042'],
  'out_name': 'hangar_weap_f20a_spw2_401510E242FD8149.png',
  'weap_fn': 'b95d58c0d63aea15-9ab5acb80064b414-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['6542C1C2796216A9', 'ACB2121F489073E1', '6221B027AF486F45'],
  'out_name': 'hangar_weap_f16c_spw2_6542C1C2796216A9.png',
  'weap_fn': 'd2379ebef5fc21e2-92e8d3a22733591b-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['57DA7480F4C5C174', 'C60B5926AF55594C', 'E0B24080B18414EB'],
  'out_name': 'hangar_weap_m31b_spw2_57DA7480F4C5C174.png',
  'weap_fn': '2722168c224dda31-fb2acc81acbc589-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['3D49A5B6B0B0E41D', 'EE4F41A29467A525', '0BEB5A81533D93D2'],
  'out_name': 'hangar_weap_mr2d_spw2_3D49A5B6B0B0E41D.png',
  'weap_fn': '411cc74c13352163-fe926e6c6d66aba3-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['732D3C53673CB93B', 'B07621AC83E99583', '8AF23142736508A2'],
  'out_name': 'hangar_weap_su27_spw2_732D3C53673CB93B.png',
  'weap_fn': '197ae439eef759e1-b364924c70000301-00001553.png'},
 {'dst_320': (64, 16, 248, 304),
  'hashes': ['BD76A1C00744D03A', 'EE5789A0B51E2042', 'B12158A3D18C3E6D'],
  'out_name': 'hangar_weap_tnd4_spw2_BD76A1C00744D03A.png',
  'weap_fn': '52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['49D93438073C416D', '9BBABABB5E8DF535', 'C43E400CBF22CBCD'],
  'out_name': 'hangar_weap_f02a_spw2_49D93438073C416D.png',
  'weap_fn': '1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png'},
 {'dst_320': (136, 16, 184, 304),
  'hashes': ['B0FB9A6F2154ED62', '88D308F3047C43EA', '1D5F5C10799D4C54'],
  'out_name': 'hangar_weap_f35c_spw2_B0FB9A6F2154ED62.png',
  'weap_fn': '43ef3bb8935a13d6-a797282f96013320-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['66DD7F6DF4DE1847', 'ADB0814BCE0A71DF', '273345FC5D3D2D04'],
  'out_name': 'hangar_weap_rm01_spw2_66DD7F6DF4DE1847.png',
  'weap_fn': 'b3cd052625961d0e-ab103eeda9f5b27a-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['6E1F6A8FEC33D8EB', '422CD1B46259BB83', 'B2CBC3265C762CF2'],
  'out_name': 'hangar_weap_typn_spw2_6E1F6A8FEC33D8EB.png',
  'weap_fn': '2c5a883bac7f9d52-d769d53355989eb2-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['F9F02B902A923C3F', '956A11DE849395A7', '942318A6932F2E0F'],
  'out_name': 'hangar_weap_su32_spw2_F9F02B902A923C3F.png',
  'weap_fn': '350eb8028387d459-de67f70996c01abf-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['E1F1837ABD30B33E', 'ECC0538A20F24E86', '1B2DF96BED2A43C6'],
  'out_name': 'hangar_weap_yf23_spw2_E1F1837ABD30B33E.png',
  'weap_fn': '4664458b39154bf4-d16bf124bac220ff-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['E797023306AE8D13', 'D748E3F072A6E78B', 'E437E0824462B823'],
  'out_name': 'hangar_weap_su47_spw2_E797023306AE8D13.png',
  'weap_fn': '52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['F6D1B38CE245CA6A', 'AB2DAC67FA2CDF22', 'A9C693E92E829941'],
  'out_name': 'hangar_weap_su37_spw2_F6D1B38CE245CA6A.png',
  'weap_fn': '9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['649644B0FFDB35DB', 'EA700DCC7B06BA83', '721A6A3A5B590688'],
  'out_name': 'hangar_weap_mrgn_spw2_649644B0FFDB35DB.png',
  'weap_fn': '1cf7f6ad376f713e-62852b858c81242b-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['30844AB64E1F2249', '904D8489F9B208E1', 'F205ABA8380A7AB0'],
  'out_name': 'hangar_weap_f05e_spw3_30844AB64E1F2249.png',
  'weap_fn': 'c343a8decadc17f5-8cc6e211a14a059e-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['25FAE3655BFFA8F3', '5FDB3AAF55A35F3B', '68187FDBE16C4BAD'],
  'out_name': 'hangar_weap_j35j_spw3_25FAE3655BFFA8F3.png',
  'weap_fn': '8448b79b04c936f4-13673bbe6030c235-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['B1A58297257ADB7A', '7C130833048EB7D2', '39C43B56DFE3005A'],
  'out_name': 'hangar_weap_f18c_spw3_B1A58297257ADB7A.png',
  'weap_fn': 'd6253421a2720ba3-ee47d64d1554ab8c-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['4E3D13DE1C6F5733', '75D92B4F93526B6B', '1FEBC754AB857C5D'],
  'out_name': 'hangar_weap_a10a_spw3_4E3D13DE1C6F5733.png',
  'weap_fn': '5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['DD6B70BA361EE20B', '0AB8534738E85B33', '44CC8D01D8FA7044'],
  'out_name': 'hangar_weap_mr2d_spw3_DD6B70BA361EE20B.png',
  'weap_fn': 'bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png'},
 {'dst_320': (104, 16, 216, 304),
  'hashes': ['C0443621B3B2DCF2', '97487A889B30B19A', 'DD39B7DDB5874A01'],
  'out_name': 'hangar_weap_j39c_spw3_C0443621B3B2DCF2.png',
  'weap_fn': 'e37af4f2dca452d2-ca27388b97df0ca-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['1812447569099D16', 'A4ED02770ECC7BCE', 'B089100270189D30'],
  'out_name': 'hangar_weap_tnd4_spw3_1812447569099D16.png',
  'weap_fn': '78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['90BBA7B3886A4A14', '99AA4D1573E7863C', '1001C1AFBD49C72B'],
  'out_name': 'hangar_weap_typn_spw3_90BBA7B3886A4A14.png',
  'weap_fn': 'f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png'},
 {'dst_320': (120, 16, 200, 304),
  'hashes': ['1ED7D11252A2812E', '9293110B39C6F846', '0AB8A82F8DFEA912'],
  'out_name': 'hangar_weap_yf23_spw3_1ED7D11252A2812E.png',
  'weap_fn': '3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png'},
 {'dst_320': (112, 16, 200, 304),
  'hashes': ['961BAC79F6935C20', '20EC5CEE7EA32788', 'AF659F99289B8E4D'],
  'out_name': 'hangar_weap_su47_spw3_961BAC79F6935C20.png',
  'weap_fn': 'dfe97d3b32645f46-7cea02778833eddf-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['1D9CF7D49090C2E2', '245FE59D4755B63A', 'E0204A84031A9AF1'],
  'out_name': 'hangar_weap_su37_spw3_1D9CF7D49090C2E2.png',
  'weap_fn': '1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png'},
 {'dst_320': (112, 16, 208, 304),
  'hashes': ['D5BFAA3A3EBCFEFA', 'C05C5FC2D7BB8E42', '3065051E0F54F819'],
  'out_name': 'hangar_weap_fa22_spw3_D5BFAA3A3EBCFEFA.png',
  'weap_fn': '294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png'},
 {'dst_320': (128, 16, 192, 304),
  'hashes': ['36C85406DE6504F4', 'B2FEF6B7DA7F126C', 'DF5D5B50938D6A75'],
  'out_name': 'hangar_weap_x02a_spw3_36C85406DE6504F4.png',
  'weap_fn': 'ba57f120b72bdc35-912159579341a858-00001553.png'},
 {'dst_320': (96, 16, 224, 304),
  'hashes': ['8A1127F62E6AC9BE', '2BE57315A7502046', 'F534D6B4894E144F'],
  'out_name': 'hangar_weap_mrgn_spw3_8A1127F62E6AC9BE.png',
  'weap_fn': '3073f6813e500d9a-f09687360676a0f2-00001553.png'}]

HANGAR_TIM2_DIAG_SPECS = [{'cid': 7,
  'dst_512': (56, 136, 456, 376),
  'hashes': ['5ABB139679DF8C3A', '53B1C19300561FA2', '6616704A6FCB1613'],
  'out_name': 'hangar_diag_07_e92_5ABB139679DF8C3A.png'},
 {'cid': 11,
  'dst_512': (56, 128, 464, 408),
  'hashes': ['80761C7BA8C74B26', 'D2B742365D1FA1AE', '1018FB8F0073326E'],
  'out_name': 'hangar_diag_11_e93_80761C7BA8C74B26.png'},
 {'cid': 14,
  'dst_512': (48, 128, 456, 392),
  'hashes': ['51AE0AACDD342738', '1FA0379AC5A3F4A0', '975812E423A05468'],
  'out_name': 'hangar_diag_14_e94_51AE0AACDD342738.png'},
 {'cid': 9,
  'dst_512': (48, 88, 464, 424),
  'hashes': ['372C93B73D452D22', '51F18E48C3B17F3A', '5F3183E9BBDBD7BF'],
  'out_name': 'hangar_diag_09_e95_372C93B73D452D22.png'},
 {'cid': 1,
  'dst_512': (56, 120, 464, 392),
  'hashes': ['1E767AECF138302E', '3FD506797F254496', '18D234B1CA1AAF4C'],
  'out_name': 'hangar_diag_01_e96_1E767AECF138302E.png'},
 {'cid': 16,
  'dst_512': (56, 128, 464, 384),
  'hashes': ['CC2674D8F5F15135', 'CE126CABCAAC2D5D', '52ED2972602A30B1'],
  'out_name': 'hangar_diag_16_e97_CC2674D8F5F15135.png'},
 {'cid': 12,
  'dst_512': (56, 120, 464, 384),
  'hashes': ['D62CE3B95A1BEF94', '85EC8D287CB8162C', '2F3885164A84A8E6'],
  'out_name': 'hangar_diag_12_e98_D62CE3B95A1BEF94.png'},
 {'cid': 2,
  'dst_512': (56, 104, 464, 408),
  'hashes': ['67D7BA13ECAB40B6', 'BF973B29A8FEEEAE', '46E9AADE6F881EC8'],
  'out_name': 'hangar_diag_02_e99_67D7BA13ECAB40B6.png'},
 {'cid': 15,
  'dst_512': (48, 120, 464, 392),
  'hashes': ['6B9A1945EA61DC86', '1147F6DADD03497E', '4F642A78161B2F91'],
  'out_name': 'hangar_diag_15_e100_6B9A1945EA61DC86.png'},
 {'cid': 8,
  'dst_512': (56, 120, 464, 400),
  'hashes': ['85B0B814F4A32F6C', 'D4E88433A8C8D0D4', 'DD8FF2B25D22F48B'],
  'out_name': 'hangar_diag_08_e101_85B0B814F4A32F6C.png'},
 {'cid': 4,
  'dst_512': (56, 96, 464, 424),
  'hashes': ['1A045EDF5F5FB97B', 'FAE83DACDB9E5803', 'CD003199C0B4C79C'],
  'out_name': 'hangar_diag_04_e102_1A045EDF5F5FB97B.png'},
 {'cid': 0,
  'dst_512': (56, 72, 464, 440),
  'hashes': ['07BA923F19F49884', '7CB729C14396C59C', '42298569F1870559'],
  'out_name': 'hangar_diag_00_e103_07BA923F19F49884.png'},
 {'cid': 6,
  'dst_512': (40, 104, 456, 400),
  'hashes': ['CB36C9D7E460ABFF', 'DA0C8B3FA814DDF7', '86B2A25E4F175437'],
  'out_name': 'hangar_diag_06_e104_CB36C9D7E460ABFF.png'},
 {'cid': 13,
  'dst_512': (32, 104, 456, 400),
  'hashes': ['847C878E5961CC21', '20E4A5A83A79BC59', 'F9D9DF74D54980F1'],
  'out_name': 'hangar_diag_13_e105_847C878E5961CC21.png'},
 {'cid': 10,
  'dst_512': (40, 128, 464, 384),
  'hashes': ['2F1B879F052F7070', '764167A720266D78', 'FB8713221D35254C'],
  'out_name': 'hangar_diag_10_e106_2F1B879F052F7070.png'},
 {'cid': 5,
  'dst_512': (32, 120, 480, 392),
  'hashes': ['2E9EE736675B9EC3', '1714B347D2377B9B', 'ACDE72AF68D20161'],
  'out_name': 'hangar_diag_05_e107_2E9EE736675B9EC3.png'},
 {'cid': 3,
  'dst_512': (56, 104, 448, 408),
  'hashes': ['C33CE224CAE5CB78', '0B0D6CA90DA5AE10', 'C115D46D7C01D715'],
  'out_name': 'hangar_diag_03_e108_C33CE224CAE5CB78.png'},
 {'cid': 17,
  'dst_512': (32, 224, 472, 288),
  'hashes': ['AB94EC6D96B38BA4', 'B82928DC8A76E0FC', 'DA3A3E83AF5606EC'],
  'out_name': 'hangar_diag_17_e109_AB94EC6D96B38BA4.png'},
 {'cid': 18,
  'dst_512': (56, 136, 456, 376),
  'hashes': ['5296FDD584CF77FA', 'C68CAE8D1639DB62', 'DAF486E54A19AF25'],
  'out_name': 'hangar_diag_18_e110_5296FDD584CF77FA.png'}]

FALLBACK_UGBS_1X_B64 = "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAABi0lEQVR4nL3XPUscQRjA8Z9yRYpUKSwsLEI4EkWQgHbmBY58Ar9ByhRWFiLCgaVICpEUKYSkSiVCihAIhJCAvbl4iJUgWIulHBYzwmLWZW+9maeZnef1z8wyz8xYt9t1D+nFcaZpgvF7FN/HIObYzw3QwWMs4x3aWMoJ8B6/CvPP2MgJ0MJeYf5T2I4sAJP4d6vgg6ibzAHQwWmJ/hzPcwCs4qxE38NmDoABvpboD3CVGuBNHMv2+lHM10kJsFjD52VKgDb6FfZ+9EkGADsNbSMDGKkMCzBdw+dpSoCLGj6XKQEusVBhX0gNcIH5Cvt8aoC+6j1u40dKgD+q224LJykBvsWYqRLbhNALvqcEgGOslOjXhYY0lDQB2LsjboCtHACfhKW+HTuuuk+MDIDwszW6A44KgND/byDaTXM1BXiG14X42ajLBgCvCt8vmiZpCnCE34X5XxzmAujFgl8Kuo/CCTg0xLAAh8KT7EOJbVs4iHoltjulhYfqX6fXsIs5/8MP8ARvhZdSrfrXpSZEY8AoqEUAAAAASUVORK5CYII="
FALLBACK_MPBM_DESC_1X_B64 = "iVBORw0KGgoAAAANSUhEUgAAAFAAAABACAYAAACNx/A2AAAD20lEQVR4nO2aP0xTURSHP4yjhoGlLAQ2MSYEFh3cKAuLbDqUhcRQJoyTTrhAjJNMwuICAyYmwmgKM8EBB/8w0ri0mxJxxuGcx3t9bQF77is8c76kae+9597e9+u9551zX3sWFhZwOufaZU8g77iARlxAIy6gERfQiAtoxAU04gIayVrAIrAJHAIn+r6p9f8FWQlYQgSrACPAR2Ba30e0/kTtcs31DMbcB0aBVaCcaltPfF4B1oCnwFgG8+gKoVfgISLebZrFS1NWu1Htl0tCCrgJDCJb9eCCfX6q/aD2zx2hBBwGHiBirCErqggUWtgW9FUEdtX+ifYfDjSfrhFKwF1gC/FxR8iKqgAzwN2UbV3rK2p3BLzT/ruB5tM1Qgg4DPQS+7w3ibYPwF6LPm8Tn18hopZ1nFytwhACzgNVRASAHX2vAt9p3sYFoKbtEItZ17r5AHNqRwEJnVq5lo4IIeAjYCNR/g30A0NaTm/LqDwE9KTaNnS8YBeoFJHwqgb0Ef/YZqwCFpBtt5Oo2yOeYD/i5/a1vK/l/oR98mJ2dLw7xnlFc5sHfiH+dlTrlwOMfYpVwOhCt9u014EJ4lhvVMvtVkA0TnSn7oQCEhLVgNeIT+5HIoQjtSnp60aH33FKNw4TtoGXyMqDOI1r9wIJbWrn2LXrewwMJL7/m9b1IXFnNP4acM96cVmkcmmKwDPkBjGIpHg/zrBfREKaT//4PX+Q1VdHUsMCcboIsvp69XPa93aMVcCv+l6k9TYuICtuC5hCttYssqVabePolOY9jXlzJ9T1OyNfOKf1pQBjn2LdwnXklx1v0ZYMV6a0bkrLNcT/pI+1xnW8YBeoc3wO3EJcyf2AYwfxgRvEvy5I5nFCY7iSJCp/QVZn8mYxR2NIFJJIyBdcsThwGfEt0R0tWo2DNIYrSaLwBiStgzgkChpmtKDOFYoDQU5eqsTb7nGibabJurl+kdjhV7n4Sc6VIFQYM4mcppSQlVVFzvqW2tgvATeJ07mH2n8y0Hy6Rqgw5gAJT9aQQLldYJ3kGPGHw0jOvErOVh+EDaTLyIqqcPGHRkVEvCrnn2BfSUJnIkPAZ0TElTPsIp9XUfv0nTo3ZJHKjSF55yyNjzJLxI84a9o+TY4fKEF2ufA6ki5NII8yBxD/OKDlCW0PGTBfClnnwttc7IaSW/yvHUZcQCPWLXxyvkku6Ph4yyrgtLF/7rEKmPu7qBX3gUZcQCMuoBEX0IgLaMQFNOICGnEBjbiARlxAIy6gERfQiAtoxAU04gIacQGNuIBGXEAjLqARF9CIC2jEBTTiAhpxAY38BQS2xNbd1AorAAAAAElFTkSuQmCC"

# UX mapping: source PCSX2 dump -> output filename in mods\textures\ostrichmods\ux\
# Note: 000.png (d00645d18395b834) and 001.png (711475816fc05634) are loaded via acz_hdtex.dll's
# Hook_LoadTextureFromArcPath (+0x214230) into acz.exe's native extratex/hudfont/ slots without
# binding GIM hashes 1C2024A9952DBC00 / 8C7D0EAD0A3B1421, preserving the 1x PS2 VRAM texture for
# sub-region background cropping (Pause Menu, Minimap, and MISSION START boxes).
UX_SPECS = {
    "d00645d18395b834-c3c4161ae78bdabb-00002214.png": "000.png",
    "711475816fc05634-c5bfd55d3a21f4eb-00002214.png": "001.png",
}

# 1-to-1 mapping of OstrichMods ux/intro-squadron/letterhead/*.png (512x512, 8x of 64x64)
# to MASTER_EN_font_64x64.png glyph indices #00..#22 (Family 9625D33CDBDFD041: "SCHWARZE", etc.)
# Note: Indices #00..#05 ('0'..'5') in ACZero's 64x64 font are two-letter ligatures:
#   #00='LT' (GAULT), #01='WI' (WIZARD), #02='PA' (ESPADA), #03='DA' (ESPADA), #04='AU' (GAULT), #05='RU' (GRUN)
LETTERHEAD_64_MAP = {
    "58fadb6ec2a61d9-6252899fa067fd04-00001994.png": 0,   # '0' (LT)
    "b58a9ba90009c45c-6252899fa067fd04-00001994.png": 1,  # '1' (WI)
    "1889fd02183494ff-6252899fa067fd04-00001994.png": 2,  # '2' (PA)
    "6e6722250f07402e-6252899fa067fd04-00001994.png": 3,  # '3' (DA)
    "7f7605f4d5b613f9-6252899fa067fd04-00001994.png": 4,  # '4' (AU)
    "d79667bc9e8453b0-6252899fa067fd04-00001994.png": 5,  # '5' (RU)
    "638c0f748648c894-6252899fa067fd04-00001994.png": 6,  # 'A'
    "2102e1c3e0e2f987-6252899fa067fd04-00001994.png": 7,  # 'B'
    "961a2f25068c1899-6252899fa067fd04-00001994.png": 8,  # 'C'
    "ae1bd754bf602e6-6252899fa067fd04-00001994.png": 9,   # 'D'
    "2d5590226f638d2f-6252899fa067fd04-00001994.png": 10, # 'E'
    "f7a448e1576cad89-6252899fa067fd04-00001994.png": 11, # 'G'
    "d8b29df2733365e8-6252899fa067fd04-00001994.png": 12, # 'H'
    "a9057a6157fa0332-6252899fa067fd04-00001994.png": 13, # 'I'
    "e25265184d31b953-6252899fa067fd04-00001994.png": 14, # 'L'
    "c9d2fff2cbfb924c-6252899fa067fd04-00001994.png": 15, # 'M'
    "1a7e1982c1e344ac-6252899fa067fd04-00001994.png": 16, # 'N'
    "e382d2a788f7e01c-6252899fa067fd04-00001994.png": 17, # 'O'
    "dca2f51328b6cbb5-6252899fa067fd04-00001994.png": 18, # 'R'
    "68ac3905293fbb04-6252899fa067fd04-00001994.png": 19, # 'S'
    "a9fdff9a15ba5ce1-6252899fa067fd04-00001994.png": 20, # 'T'
    "dbfbb22e66f8a13-6252899fa067fd04-00001994.png": 21,  # 'W'
    "75cc3ebeec7cfe46-6252899fa067fd04-00001994.png": 22, # 'Z'
}

# Mapping of OstrichMods ux/intro-squadron/upper-num/*.png and lower/*.png (256x256 -> downsampled to 128x128 for 4x 32x32 master)
# to Unicode codepoints (cp) in Family 1C85B763AD07A4EC ("Belkan Air Force / 5th Air Division 23rd Tactical Fighter SQ").
# Note: '0' (U+0030) and 'O' (U+004F) share the exact same 1x bitmap in ACZero, so 493074ad4cf797ae maps to both!
INTRO_SUB_32_UPPER_MAP = {
    "493074ad4cf797ae-9beeeb5765c760d6-00001554.png": [0x0030, 0x004F], # '0' and 'O'
    "bf85e980913d4c1f-9beeeb5765c760d6-00001554.png": [0x0031],         # '1'
    "c5d47c911bfb6b07-9beeeb5765c760d6-00001554.png": [0x0032],         # '2'
    "14a47cee5c41ba5b-9beeeb5765c760d6-00001554.png": [0x0033],         # '3'
    "cdacb610ac12eb12-9beeeb5765c760d6-00001554.png": [0x0035],         # '5'
    "a752220b1d3a7b87-9beeeb5765c760d6-00001554.png": [0x0036],         # '6'
    "bd31563cbae5cbed-9beeeb5765c760d6-00001554.png": [0x0037],         # '7'
    "a68d85b6e6ecedca-9beeeb5765c760d6-00001554.png": [0x0038],         # '8'
    "a71eb05759eae02-9beeeb5765c760d6-00001554.png": [0x0039],          # '9'
    "323ac31e13747ea5-9beeeb5765c760d6-00001554.png": [0x0041],         # 'A'
    "fc0aaf344db6a7ce-9beeeb5765c760d6-00001554.png": [0x0042],         # 'B'
    "a20b20b26d305893-9beeeb5765c760d6-00001554.png": [0x0044],         # 'D'
    "b4cdad0dd55b687c-9beeeb5765c760d6-00001554.png": [0x0046],         # 'F'
    "acb25de2a7b81149-9beeeb5765c760d6-00001554.png": [0x004C],         # 'L'
    "c0a2948904069623-9beeeb5765c760d6-00001554.png": [0x004E],         # 'N'
    "511137132ebab7ea-9beeeb5765c760d6-00001554.png": [0x0051],         # 'Q'
    "f99e77e5e1e9e66c-9beeeb5765c760d6-00001554.png": [0x0053],         # 'S'
    "a15e9cf74bd4387f-9beeeb5765c760d6-00001554.png": [0x0054],         # 'T'
    "b6d3b7054751bad2-9beeeb5765c760d6-00001554.png": [0x0055],         # 'U'
}

INTRO_SUB_32_LOWER_MAP = {
    "89387acd9be7996d-9beeeb5765c760d6-00001554.png": [0x0061],         # 'a'
    "2eff1c5b86bed65a-9beeeb5765c760d6-00001554.png": [0x0063],         # 'c'
    "1ec20f52cfbd021a-9beeeb5765c760d6-00001554.png": [0x0064],         # 'd'
    "9c94881fe89f767b-9beeeb5765c760d6-00001554.png": [0x0065],         # 'e'
    "4783dd6356dec740-9beeeb5765c760d6-00001554.png": [0x0066],         # 'f'
    "e253fc86a099592-9beeeb5765c760d6-00001554.png": [0x0067],          # 'g'
    "6e4ecec51b969db4-9beeeb5765c760d6-00001554.png": [0x0068],         # 'h'
    "71ae3d13e0d6ddc0-9beeeb5765c760d6-00001554.png": [0x0069],         # 'i'
    "4733cacad6594289-9beeeb5765c760d6-00001554.png": [0x006B],         # 'k'
    "230b90de25623b38-9beeeb5765c760d6-00001554.png": [0x006C],         # 'l'
    "baffbd778f7307b5-9beeeb5765c760d6-00001554.png": [0x006E],         # 'n'
    "f7cb37ae071703cf-9beeeb5765c760d6-00001554.png": [0x006F],         # 'o'
    "7049e97d81353cd9-9beeeb5765c760d6-00001554.png": [0x0070],         # 'p'
    "e056357db951de7a-9beeeb5765c760d6-00001554.png": [0x0072],         # 'r'
    "eb96df5a4ee6f8aa-9beeeb5765c760d6-00001554.png": [0x0073],         # 's'
    "55a6c2c702a155e0-9beeeb5765c760d6-00001554.png": [0x0074],         # 't'
    "4d8f5224e405ee90-9beeeb5765c760d6-00001554.png": [0x0076],         # 'v'
}


def write_companion_json(png_path: str, data: dict) -> None:
    json_path = os.path.splitext(png_path)[0] + ".json"
    with open(json_path, "w", encoding="utf-8") as jf:
        json.dump(data, jf, indent=2)


def ps2_alpha_to_pc_array(rgba: np.ndarray) -> np.ndarray:
    """Restores PS2 GS alpha (0..128) to full PC alpha (0..255) while preserving RGB colors."""
    out = rgba.copy()
    alpha = out[:, :, 3].astype(np.uint32)
    if alpha.max() <= 180:
        out[:, :, 3] = np.minimum(255, (alpha * 255 + 64) // 128).astype(np.uint8)
    return out


def resize_premultiplied_lanczos(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Resizes an RGBA image using premultiplied alpha to prevent dark/white fringe bleeding."""
    arr = np.array(img, dtype=np.float32)
    alpha = arr[:, :, 3:4]/ 255.0
    premul_rgb = np.clip(np.round(arr[:, :, :3] * alpha), 0, 255).astype(np.uint8)
    alpha_u8 = np.clip(np.round(arr[:, :, 3]), 0, 255).astype(np.uint8)

    rgb_im = Image.fromarray(premul_rgb, mode="RGB").resize((target_w, target_h), Image.Resampling.LANCZOS)
    a_im = Image.fromarray(alpha_u8, mode="L").resize((target_w, target_h), Image.Resampling.LANCZOS)

    rgb_res = np.array(rgb_im, dtype=np.float32)
    a_res = np.array(a_im, dtype=np.float32)
    a_norm = a_res[:, :, None]/ 255.0

    unpremul_rgb = np.where(a_norm > (1.0/ 255.0), rgb_res/ np.maximum(a_norm, 1e-5), 0.0)
    out = np.zeros((target_h, target_w, 4), dtype=np.uint8)
    out[:, :, :3] = np.clip(np.round(unpremul_rgb), 0, 255).astype(np.uint8)
    out[:, :, 3] = np.clip(np.round(a_res), 0, 255).astype(np.uint8)
    return Image.fromarray(out, mode="RGBA")


def process_aircraft_icon(png_bytes: bytes) -> Image.Image:
    """
    Crops 384x384 PCSX2 GS dump to 288x288 (6x of 48x48) and restores standard PS2->PC alpha (0..128 -> 0..255).
    """
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    if im.size == (384, 384):
        im = im.crop((0, 0, 288, 288))
    elif im.width == im.height and im.width % 64 == 0:
        crop_dim = (im.width * 48) // 64
        im = im.crop((0, 0, crop_dim, crop_dim))
    arr = ps2_alpha_to_pc_array(np.array(im))
    return Image.fromarray(arr, mode="RGBA")

def process_weapon_icon(png_bytes: bytes) -> Image.Image:
    """Restores PS2->PC alpha (0..128 -> 0..255) and flat RGB=128 for standalone 256x256 weapon icons."""
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = ps2_alpha_to_pc_array(np.array(im))
    mask = arr[:, :, 3] > 0
    arr[mask, :3] = 128
    arr[~mask, :3] = 0
    return Image.fromarray(arr, mode="RGBA")


def process_custom_ugbs_icon(png_bytes: bytes) -> Image.Image:
    """
    Processes custom HD 3B97DB2F5EB61035.png (UGBS bomb icon) into a centered 256x256 icon
    while preserving its translucent interior fill and flat RGB=128 without dark fringe bleeding.
    """
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = np.array(im).copy()
    # Clean faint background noise (alpha <= 18) outside the bomb body while preserving interior fill (~28..30)
    arr[arr[:, :, 3] <= 18, 3] = 0
    h, w = arr.shape[:2]

    a = arr[:, :, 3].astype(np.float32)
    nz = a[a > 0]
    if len(nz) > 0 and a.max() <= 180:
        p95 = max(125.0, float(np.percentile(nz, 95)))
        a = np.minimum(255.0, (a * 255.0) / p95)
    a_u8 = np.clip(np.round(a), 0, 255).astype(np.uint8)

    if (w, h) != (256, 256):
        scale = min(256.0 / w, 256.0 / h)
        nw = int(round(w * scale))
        nh = int(round(h * scale))
        a_res = np.array(
            Image.fromarray(a_u8, mode="L").resize((nw, nh), Image.Resampling.LANCZOS),
            dtype=np.uint8,
        )
        out = np.zeros((256, 256, 4), dtype=np.uint8)
        ox = (256 - nw) // 2
        oy = (256 - nh) // 2
        out[oy : oy + nh, ox : ox + nw, 3] = a_res
    else:
        out = np.zeros((256, 256, 4), dtype=np.uint8)
        out[:, :, 3] = a_u8

    mask = out[:, :, 3] > 0
    out[mask, :3] = 128
    return Image.fromarray(out, mode="RGBA")


def boost_weapon_icon_for_hangar_atlas(im_weap: Image.Image) -> Image.Image:
    """
    Boosts the weapon icon interior alpha from standalone HUD level (~62) to Hangar Atlas level (~183)
    while keeping the outline at 255 and flat RGB=128, matching original 256x128 Hangar Atlas dumps.
    """
    arr = np.array(im_weap).copy()
    a = arr[:, :, 3].astype(np.float32)
    a_boost = np.where(
        a <= 62.0,
        a * (183.0 / 62.0),
        183.0 + (a - 62.0) * (255.0 - 183.0) / (255.0 - 62.0),
    )
    arr[:, :, 3] = np.clip(np.round(a_boost), 0, 255).astype(np.uint8)
    mask = arr[:, :, 3] > 0
    arr[mask, :3] = 128
    arr[~mask, :3] = 0
    return Image.fromarray(arr, mode="RGBA")


def process_ux_atlas(png_bytes: bytes) -> Image.Image:
    """
    Restores PS2->PC alpha (0..128 -> 0..255) and sets RGB=(105, 105, 105) on all pixels
    (including transparent A=0 pixels) to match the original ACZ HUD font atlas luminance
    and prevent dark bilinear filtering fringes around HUD glyphs and frames.
    """
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = ps2_alpha_to_pc_array(np.array(im))
    arr[:, :, :3] = 105
    return Image.fromarray(arr, mode="RGBA")


def process_intro_glyph_tile(png_bytes: bytes, target_rgb: int) -> Image.Image:
    """
    Restores PS2->PC alpha (0..128 -> 0..255) and sets uniform luminance RGB=(target_rgb, target_rgb, target_rgb)
    on all non-transparent pixels for 8x Squadron Intro font tiles (512x512 letterhead or 256x256 lower/upper-num).
    """
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = ps2_alpha_to_pc_array(np.array(im))
    mask = arr[:, :, 3] > 0
    arr[mask, :3] = target_rgb
    arr[~mask, :3] = 0
    return Image.fromarray(arr, mode="RGBA")


def process_squadron_emblem(
    png_bytes: bytes,
    src_bbox: tuple,
    dst_bbox_512: tuple,
    align_bbox: bool = True,
) -> Image.Image:
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = ps2_alpha_to_pc_array(np.array(im))
    im_pc = Image.fromarray(arr, mode="RGBA")

    if not align_bbox or im_pc.size != (512, 512):
        return im_pc

    sx0, sy0, sx1, sy1 = src_bbox
    dx0, dy0, dx1, dy1 = dst_bbox_512
    target_w = dx1 - dx0
    target_h = dy1 - dy0

    cropped = im_pc.crop((sx0, sy0, sx1, sy1))
    resized = resize_premultiplied_lanczos(cropped, target_w, target_h)

    canvas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    canvas.paste(resized, (dx0, dy0))
    return canvas


def process_ac5_description_diagram(
    png_bytes: bytes,
    src_bbox: tuple,
    dst_bbox_640x512: tuple,
) -> Image.Image:
    """
    Crops an SPW weapon description diagram from mods\\textures\\ostrichmods\\icons-weapons\\spw,
    normalizes its alpha to full PC range (0..255) matching original ACZ Hangar Atlas opacity,
    and sets uniform flat RGB=(128, 128, 128) on all non-transparent pixels.
    """
    im = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    arr = np.array(im)
    a = arr[:, :, 3].astype(np.float32)
    nz = a[a > 0]
    if len(nz) > 0:
        p80 = float(np.percentile(nz, 80))
        if p80 > 0:
            a = np.minimum(255.0, a * (255.0 / p80) * 1.15)
    a_u8 = np.clip(np.round(a), 0, 255).astype(np.uint8)
    a_im = Image.fromarray(a_u8, mode="L")

    sx0, sy0, sx1, sy1 = src_bbox
    dx0, dy0, dx1, dy1 = dst_bbox_640x512
    target_w = dx1 - dx0
    target_h = dy1 - dy0

    if a_im.size == (512, 512):
        cropped_a = a_im.crop((sx0, sy0, sx1, sy1))
    else:
        scale_x = a_im.width / 80.0
        scale_y = a_im.height / 64.0
        cx0 = int(round((dx0 / 8.0) * scale_x))
        cy0 = int(round((dy0 / 8.0) * scale_y))
        cx1 = int(round((dx1 / 8.0) * scale_x))
        cy1 = int(round((dy1 / 8.0) * scale_y))
        cropped_a = a_im.crop((max(0, cx0), max(0, cy0), min(a_im.width, cx1), min(a_im.height, cy1)))

    res_a = np.array(cropped_a.resize((target_w, target_h), Image.Resampling.LANCZOS), dtype=np.float32)
    res_a = np.minimum(255.0, res_a * 1.25)
    res_a_u8 = np.clip(np.round(res_a), 0, 255).astype(np.uint8)

    out = np.zeros((target_h, target_w, 4), dtype=np.uint8)
    mask = res_a_u8 > 0
    out[mask, :3] = 128
    out[:, :, 3] = res_a_u8
    return Image.fromarray(out, mode="RGBA")


def get_candidate_search_dirs(script_dir: str, extra_dir: str = "") -> list:
    orig_cwd = os.getcwd()
    dirs = []
    bases = [orig_cwd, script_dir, os.path.dirname(os.path.abspath(script_dir))]
    if extra_dir:
        bases.insert(0, os.path.abspath(extra_dir))
        bases.append(os.path.dirname(os.path.abspath(extra_dir)))
    for base in bases:
        b_abs = os.path.abspath(base)
        if b_abs not in dirs:
            dirs.append(b_abs)
        mods_sub = os.path.join(b_abs, "mods")
        if os.path.isdir(mods_sub) and mods_sub not in dirs:
            dirs.append(mods_sub)
    return dirs


def find_default_input_source(script_dir: str) -> str:
    orig_cwd = os.getcwd()
    search_dirs = get_candidate_search_dirs(script_dir)

    for d in search_dirs:
        candidates = sorted(glob.glob(os.path.join(d, "actrilogy-hud-upgrade*.zip")))
        if candidates:
            return candidates[-1]
        slus_dir = os.path.join(d, "SLUS-21346", "replacements", "hud-upgrade")
        if os.path.isdir(slus_dir):
            return d

    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        selected = filedialog.askopenfilename(
            title="Select actrilogy-hud-upgrade-v1.2.1.zip (OstrichMods PCSX2 Pack)",
            filetypes=[("ZIP Archives", "*.zip"), ("All Files", "*.*")],
        )
        root.destroy()
        os.chdir(orig_cwd)
        if selected and os.path.exists(selected):
            return selected
    except Exception:
        try:
            os.chdir(orig_cwd)
        except Exception:
            pass

    return ""


def find_texture_converter_zip(script_dir: str, input_path: str = "") -> str:
    search_dirs = get_candidate_search_dirs(script_dir, os.path.dirname(os.path.abspath(input_path)) if input_path else "")
    for d in search_dirs:
        for exact_name in ["custom textures.zip", "custom_textures.zip", "texture converter.zip", "texture_converter.zip", "mods.zip"]:
            exact_p = os.path.join(d, exact_name)
            if os.path.isfile(exact_p):
                return exact_p
        for pat in ["custom textures*.zip", "custom_textures*.zip", "texture converter*.zip", "texture_converter*.zip", "mods*.zip"]:
            cands = sorted(glob.glob(os.path.join(d, pat)))
            if cands:
                return cands[-1]
        if os.path.isdir(os.path.join(d, "For OstrichMods")):
            return d
    return ""


def resolve_mods_textures_root(output_dir: str, script_dir: str) -> str:
    out_abs = os.path.abspath(output_dir)
    if os.path.basename(os.path.normpath(out_abs)).lower() == "ostrichmods":
        return os.path.dirname(out_abs)
    for base in [script_dir, os.getcwd()]:
        b_abs = os.path.abspath(base)
        b_name = os.path.basename(os.path.normpath(b_abs)).lower()
        if b_name == "textures":
            return b_abs
        if b_name == "mods":
            return os.path.join(b_abs, "textures")
        if (
            os.path.isdir(os.path.join(b_abs, "mods", "textures"))
            or os.path.isfile(os.path.join(b_abs, "mods", "acz_hdtex.dll"))
            or os.path.isfile(os.path.join(b_abs, "acz.exe"))
        ):
            return os.path.join(b_abs, "mods", "textures")
    return os.path.join(os.path.abspath(script_dir), "mods", "textures")


def install_direct_textures_from_sources(tc_zip_path: str, script_dir: str, mods_textures_dir: str) -> int:
    """
    Copies/moves all files from the `textures\` folder inside `custom textures.zip`
    (or from a local `For OstrichMods/../textures` staging folder) directly into `mods\textures\`.
    """
    os.makedirs(mods_textures_dir, exist_ok=True)
    moved_count = 0

    # 1. Extract `textures/...` from custom textures.zip if present
    if tc_zip_path and os.path.isfile(tc_zip_path) and zipfile.is_zipfile(tc_zip_path):
        with zipfile.ZipFile(tc_zip_path, "r") as zf:
            for info in zf.infolist():
                if info.is_dir():
                    continue
                norm_p = info.filename.replace("\\", "/").lstrip("/")
                parts = norm_p.split("/")
                tex_idx = -1
                for idx, part in enumerate(parts[:-1]):
                    if part.lower() == "textures" and (idx == 0 or parts[idx - 1].lower() != "mods"):
                        tex_idx = idx
                        break
                if tex_idx != -1:
                    rel_sub = os.path.join(*parts[tex_idx + 1 :])
                    dst_p = os.path.join(mods_textures_dir, rel_sub)
                    os.makedirs(os.path.dirname(dst_p), exist_ok=True)
                    with open(dst_p, "wb") as fp:
                        fp.write(zf.read(info.filename))
                    moved_count += 1

    # 2. Move files from local `textures\` folder if it is a staging folder next to `For OstrichMods\`
    cand_bases = []
    if tc_zip_path and os.path.isdir(tc_zip_path):
        cand_bases.append(os.path.abspath(tc_zip_path))
    for d in get_candidate_search_dirs(script_dir):
        if d not in cand_bases:
            cand_bases.append(d)

    mods_tex_abs = os.path.abspath(mods_textures_dir)
    for base in cand_bases:
        local_tex_dir = os.path.abspath(os.path.join(base, "textures"))
        if not os.path.isdir(local_tex_dir) or local_tex_dir == mods_tex_abs:
            continue
        # Only treat `./textures` as a staging folder if `For OstrichMods` is also in `base`
        # or if `base` is not `mods`
        if os.path.basename(os.path.normpath(base)).lower() == "mods" and not os.path.isdir(os.path.join(base, "For OstrichMods")):
            continue
        for root, _, files in os.walk(local_tex_dir):
            for fn in files:
                src_f = os.path.join(root, fn)
                rel_f = os.path.relpath(src_f, local_tex_dir)
                dst_f = os.path.join(mods_textures_dir, rel_f)
                if os.path.abspath(src_f) == os.path.abspath(dst_f):
                    continue
                os.makedirs(os.path.dirname(dst_f), exist_ok=True)
                shutil.move(src_f, dst_f)
                moved_count += 1

    return moved_count


def resolve_default_output_dir(script_dir: str) -> str:
    for base in [script_dir, os.getcwd()]:
        b_abs = os.path.abspath(base)
        b_name = os.path.basename(os.path.normpath(b_abs)).lower()
        if b_name == "ostrichmods":
            return b_abs
        if b_name == "textures":
            return os.path.join(b_abs, "ostrichmods")
        if b_name == "mods":
            return os.path.join(b_abs, "textures", "ostrichmods")
        if (
            os.path.isdir(os.path.join(b_abs, "mods", "textures"))
            or os.path.isfile(os.path.join(b_abs, "mods", "acz_hdtex.dll"))
            or os.path.isfile(os.path.join(b_abs, "acz.exe"))
        ):
            return os.path.join(b_abs, "mods", "textures", "ostrichmods")
    return os.path.join(os.path.abspath(script_dir), "textures", "ostrichmods")


def load_source_files(input_path: str, tc_zip_path: str, spw_dir: str) -> tuple:
    """
    Loads:
      1. OstrichMods ACZero HUD & Intro-Squadron textures from `SLUS-21346/replacements/hud-upgrade/` inside `input_path`.
      2. SPW weapon diagrams + MASTER_EN_font_* files from `mods\texture converter.zip` (`tc_zip_path`)
         with fallback to `spw_dir` (`mods\textures\ostrichmods\icons-weapons\spw`) if present.
    """
    hud_files = {}
    spw_files = {}
    master_font_files = {}

    if input_path and os.path.exists(input_path):
        if os.path.isfile(input_path) and zipfile.is_zipfile(input_path):
            with zipfile.ZipFile(input_path, "r") as zf:
                for info in zf.infolist():
                    if info.is_dir():
                        continue
                    norm_name = info.filename.replace("\\", "/")
                    if not norm_name.lower().endswith(".png"):
                        continue
                    idx = norm_name.find(ACZ_SUBPATH_PREFIX)
                    if idx != -1:
                        rel = norm_name[idx + len(ACZ_SUBPATH_PREFIX) :]
                        hud_files[rel] = zf.read(info.filename)
        elif os.path.isdir(input_path):
            hud_dir = None
            cand1 = os.path.join(input_path, "SLUS-21346", "replacements", "hud-upgrade")
            cand2 = os.path.join(input_path, "replacements", "hud-upgrade")
            if os.path.isdir(cand1):
                hud_dir = cand1
            elif os.path.isdir(cand2):
                hud_dir = cand2
            elif os.path.isdir(os.path.join(input_path, "icons-aircraft")) and os.path.isdir(
                os.path.join(input_path, "icons-weapons")
            ):
                hud_dir = input_path

            if hud_dir:
                for root, _, files in os.walk(hud_dir):
                    for fn in files:
                        if not fn.lower().endswith(".png"):
                            continue
                        full_p = os.path.join(root, fn)
                        rel = os.path.relpath(full_p, hud_dir).replace("\\", "/")
                        with open(full_p, "rb") as fp:
                            hud_files[rel] = fp.read()

    stray_slus20851 = {
        "4ea2bcf12b88b332-5b222812b8aad085-00001dd3.png",
        "ddf0c0d7a75144c9-25f11bc8fcbc8d46-00001dd3.png",
    }

    # 0. If tc_zip_path is a directory (e.g. extracted custom_textures with For OstrichMods), load it FIRST!
    if tc_zip_path and os.path.isdir(tc_zip_path):
        for PriorityDir in [os.path.join(tc_zip_path, "For OstrichMods"), tc_zip_path]:
            if os.path.isdir(PriorityDir):
                for fn in sorted(os.listdir(PriorityDir)):
                    full_p = os.path.join(PriorityDir, fn)
                    if not os.path.isfile(full_p):
                        continue
                    bname_low = fn.lower()
                    if bname_low in stray_slus20851:
                        continue
                    if bname_low.startswith("master_") and (bname_low.endswith(".png") or bname_low.endswith(".txt")):
                        if fn not in master_font_files:
                            with open(full_p, "rb") as fp:
                                master_font_files[fn] = fp.read()
                    elif bname_low.endswith((".ttf", ".otf")) and bname_low not in master_font_files:
                        with open(full_p, "rb") as fp:
                            master_font_files[bname_low] = fp.read()
                    elif bname_low.endswith(".png") and bname_low not in spw_files:
                        with open(full_p, "rb") as fp:
                            spw_files[bname_low] = fp.read()

    # 1. Read from custom textures.zip, texture converter.zip, AND mods.zip (if present in candidate directories)
    zip_candidates = []
    if tc_zip_path and os.path.isfile(tc_zip_path) and zipfile.is_zipfile(tc_zip_path):
        zip_candidates.append(os.path.abspath(tc_zip_path))
    for d in get_candidate_search_dirs(os.path.dirname(os.path.abspath(__file__)), os.path.dirname(os.path.abspath(input_path)) if input_path else ""):
        for zn in ["custom textures.zip", "custom_textures.zip", "texture converter.zip", "texture_converter.zip", "mods.zip"]:
            zp = os.path.abspath(os.path.join(d, zn))
            if os.path.isfile(zp) and zipfile.is_zipfile(zp) and zp not in zip_candidates:
                zip_candidates.append(zp)

    for zp in zip_candidates:
        with zipfile.ZipFile(zp, "r") as ztc:
            for info in ztc.infolist():
                if info.is_dir():
                    continue
                norm_fn = info.filename.replace("\\", "/")
                parts_low = [p.lower() for p in norm_fn.lstrip("/").split("/")]
                # Skip files inside `textures/` when reading For OstrichMods converter inputs
                if "textures" in parts_low[:-1] and "for ostrichmods" not in parts_low[:-1]:
                    continue
                bname = os.path.basename(norm_fn)
                bname_low = bname.lower()
                if not bname or bname_low in stray_slus20851:
                    continue
                if bname_low.startswith("master_") and (bname_low.endswith(".png") or bname_low.endswith(".txt")):
                    if bname not in master_font_files:
                        master_font_files[bname] = ztc.read(info.filename)
                elif bname_low.endswith((".ttf", ".otf")):
                    if bname_low not in master_font_files:
                        master_font_files[bname_low] = ztc.read(info.filename)
                elif bname_low.endswith(".png"):
                    if bname_low not in spw_files:
                        spw_files[bname_low] = ztc.read(info.filename)

    # 2. Also check `For OstrichMods\`, fallback spw_dir, and candidate search dirs on disk
    check_dirs = []
    if tc_zip_path and os.path.isdir(tc_zip_path):
        fom_sub = os.path.join(tc_zip_path, "For OstrichMods")
        if os.path.isdir(fom_sub):
            check_dirs.append(fom_sub)
        check_dirs.append(tc_zip_path)
    if spw_dir and os.path.isdir(spw_dir) and spw_dir not in check_dirs:
        check_dirs.append(spw_dir)
    for d in get_candidate_search_dirs(os.path.dirname(os.path.abspath(__file__)), os.path.dirname(os.path.abspath(input_path)) if input_path else ""):
        fom_d = os.path.join(d, "For OstrichMods")
        if os.path.isdir(fom_d) and fom_d not in check_dirs:
            check_dirs.append(fom_d)
        if os.path.isdir(d) and d not in check_dirs:
            check_dirs.append(d)

    for cdir in check_dirs:
        for fn in sorted(os.listdir(cdir)):
            full_p = os.path.join(cdir, fn)
            if not os.path.isfile(full_p):
                continue
            bname_low = fn.lower()
            if bname_low in stray_slus20851:
                if cdir == spw_dir:
                    try:
                        os.remove(full_p)
                    except Exception:
                        pass
                continue
            if bname_low.startswith("master_") and (bname_low.endswith(".png") or bname_low.endswith(".txt")):
                if fn not in master_font_files:
                    with open(full_p, "rb") as fp:
                        master_font_files[fn] = fp.read()
            elif bname_low.endswith((".ttf", ".otf")) and bname_low not in master_font_files:
                with open(full_p, "rb") as fp:
                    master_font_files[bname_low] = fp.read()
            elif (cdir == spw_dir or os.path.basename(os.path.normpath(cdir)).lower() == "for ostrichmods") and bname_low.endswith(".png") and bname_low not in spw_files:
                with open(full_p, "rb") as fp:
                    spw_files[bname_low] = fp.read()

    return hud_files, spw_files, master_font_files


GLYPH_LINE_RE = re.compile(
    r"^\s*Glyph #(\d+)\s+\(col=(\d+),\s*row=(\d+)\):\s+U\+([0-9A-Fa-f]{4})\s+'(.*)'\s+\|\s+adv_x=(\d+),\s*height=(\d+)\s+\|\s+TileHash=([0-9A-Fa-f]{16})\s+\|\s+ExactHash=([0-9A-Fa-f]{16})\s+\|\s+Family=([0-9A-Fa-f]{16})"
)

# Runtime 1us ExactHash aliases for 16x16 MAIN MENU (72DE1F7107135AF7 vs 12C5663260CB8DB3)
RUNTIME_16X16_ALT_EXACT_HASHES = {
    "DF341EA364C07870": "7D550476DF37D862", "993F3AB02D21247C": "6F406933E77C0316",
    "B12A5674F04705DD": "AC7C35788848582F", "A54BD270D719F185": "F3B3AD4A4B0AD42F",
    "74B4DF42C7BDFDAD": "762BD59C1DAE4D37", "2CE9DE2416310C86": "CF684C1EAA4FA4D4",
    "F139440CDDC2323E": "E63A558F224944F4", "2D8E2F344DD64B90": "325EB12591E1DD22",
    "3D3735C44B002E89": "BC0F3A542EDA471B", "C977AA3A9017C535": "A79EE531F1EEB2CB",
    "854094C4AB739F17": "41A76FF297EF7999", "AC11470E0395A1BD": "C387F2C1B2C17B53",
    "D67E78ECDB18A984": "80A6D7A9B5C1A8EE", "EACE52702EB252F1": "F32AA7466B2DE0E7",
    "E8117A7498EDE8F2": "BA3DABD88E278BC0", "F95379253669CFCE": "0421CE17BEC3E114",
    "0B6CEBB5AB1FDC64": "A1B6F4836C319A52", "02AC41DF06099FAF": "A71589FB2E4155D5",
    "913717DD40FABE58": "BF19E0AFF9AC69DE", "C7FF5BAF88E20773": "CFD692815AE7491D",
    "6BF648049A74126E": "4225ADDAF0B92480", "9028DF7F6297DE1E": "C443ADB250B7AC7C",
    "0BD511BD789206B7": "C4C1D1849AF93F99", "09DD28A8A679177A": "F2D251E560A658F8",
    "3E3D7458617C7F6D": "FBFBAE591997B8DB", "6B588A1251D02315": "D37D031AB9E67553",
    "E032BFB293FBCD84": "5EB315787DEE94DE", "21627C6A34B7B181": "4C4E75FEE079481F",
    "50C65313684426EB": "20B42018A71EB23D", "009181FA85625A75": "BEDF31C28B4E7F57",
    "BD2BA6B0C875D732": "E8C85C6DE4E51D0C", "ED845B6144F74B68": "DC5638B78F151D52",
    "593D0F1A7EA04508": "84AE13341BB4EEFE", "13AD0397F62804DD": "A2E0449256A28177",
    "FD45878F6D38C407": "F47ED412F5AC65E1", "567D031FBFA29BDB": "A18F5FE3E7BDE609",
    "01833BFD855E0355": "04086394507E027B", "2C8A0684288DA8BD": "65EF29627868DBD3",
    "F0B0AB6A5861C873": "D10A0580AA06B435", "05DC48A46D504D3C": "98E10ECC1353834E",
    "61C89F584D2AC531": "0AD9F056805E96FB", "9A8000E00DC6791F": "4FFC13DD879C6A15",
    "55AAB4ACC30DAC58": "3D067D483FE15C9A",
}

def parse_master_txt_bytes(txt_bytes: bytes, default_dim: int) -> tuple:
    entries = []
    section_meta = {}
    sample_lines = []
    mode = "header"
    for raw in txt_bytes.decode("utf-8", errors="replace").splitlines():
        if "TYPEFACE SECTIONS IN THIS MASTER ATLAS" in raw:
            mode = "sections"
            continue
        elif "SAMPLE STRINGS FROM SEEN FONTS:" in raw:
            mode = "samples"
            continue
        elif "GLYPH METRICS" in raw:
            mode = "glyphs"
            continue
        if mode == "sections":
            ms = re.search(
                r"Family=([0-9A-Fa-f]{16})(?:\s*\[\d+x\d+\])?\s*\((\d+)[^\)]*,\s*(\d+)\s*(?:blocks|блоков)\)\s*\|\s*(?:Sample|Пример):\s*\"(.*)\"",
                raw,
            )
            if ms:
                section_meta[ms.group(1).upper()] = {
                    "blocks": int(ms.group(3)),
                    "sample": ms.group(4),
                }
        elif mode == "samples":
            if raw.strip().startswith("["):
                sample_lines.append(raw)
        elif mode == "glyphs":
            m = GLYPH_LINE_RE.match(raw)
            if m:
                th = m.group(8).upper()
                eh = m.group(9).upper()
                dim_val = default_dim
                md = re.search(r"Dim=(\d+)x\d+", raw)
                if md:
                    dim_val = int(md.group(1))
                alt_set = set()
                ma = re.search(r"AltHashes=([0-9A-Fa-f,]+)", raw)
                if ma:
                    for tok in ma.group(1).split(","):
                        tok = tok.strip().upper()
                        if len(tok) == 16 and tok not in (th, eh):
                            alt_set.add(tok)
                if th in RUNTIME_16X16_ALT_EXACT_HASHES:
                    reh = RUNTIME_16X16_ALT_EXACT_HASHES[th]
                    if reh not in (th, eh):
                        alt_set.add(reh)
                entries.append({
                    "idx": int(m.group(1)),
                    "orig_idx": int(m.group(1)),
                    "col": int(m.group(2)),
                    "row": int(m.group(3)),
                    "cp": int(m.group(4), 16),
                    "ch": m.group(5),
                    "adv_x": int(m.group(6)),
                    "height": int(m.group(7)),
                    "tile_hash": th,
                    "exact_hash": eh,
                    "family": m.group(10).upper(),
                    "dim": dim_val,
                    "alt_hashes": alt_set,
                })
    return entries, section_meta, sample_lines


def deduplicate_visual_families(entries: list, sec_meta: dict) -> tuple:
    order = []
    fams = {}
    for e in entries:
        fm = e["family"]
        if fm not in fams:
            fams[fm] = []
            order.append(fm)
        fams[fm].append(e)

    merged_into = {}
    kept_order = []
    for i, fa in enumerate(order):
        if fa in merged_into:
            continue
        kept_order.append(fa)
        ga_by_cp = {g["cp"]: g for g in fams[fa]}
        for j in range(i + 1, len(order)):
            fb = order[j]
            if fb in merged_into or fams[fa][0]["dim"] != fams[fb][0]["dim"]:
                continue
            gb_list = fams[fb]
            gb_non_empty = [g for g in gb_list if g["cp"] > 0x20]
            common = [g for g in gb_non_empty if g["cp"] in ga_by_cp]
            if not common:
                continue
            sim = 0
            for g_b in common:
                g_a = ga_by_cp[g_b["cp"]]
                if abs(g_a["adv_x"] - g_b["adv_x"]) <= 1 and abs(g_a["height"] - g_b["height"]) <= 1:
                    t_a = g_a["tile_1x"].astype(np.int16)
                    t_b = g_b["tile_1x"].astype(np.int16)
                    if t_a.shape == t_b.shape and int(np.max(np.abs(t_a - t_b))) <= 32:
                        sim += 1
            if sim >= 1 and sim * 4 >= len(common) * 3:
                merged_into[fb] = fa
                sec_meta.setdefault(fa, {"blocks": 1, "sample": ""})["blocks"] += sec_meta.get(fb, {"blocks": 1})["blocks"]
                for g_b in gb_list:
                    cp = g_b["cp"]
                    if cp in ga_by_cp:
                        g_a = ga_by_cp[cp]
                        if g_b["tile_hash"] not in (g_a["tile_hash"], g_a["exact_hash"]):
                            g_a["alt_hashes"].add(g_b["tile_hash"])
                        if g_b["exact_hash"] not in (g_a["tile_hash"], g_a["exact_hash"]):
                            g_a["alt_hashes"].add(g_b["exact_hash"])
                        for ah in g_b.get("alt_hashes", set()):
                            if ah not in (g_a["tile_hash"], g_a["exact_hash"]):
                                g_a["alt_hashes"].add(ah)
                    else:
                        g_copy = dict(g_b)
                        g_copy["family"] = fa
                        g_copy["alt_hashes"] = set(g_b.get("alt_hashes", set()))
                        fams[fa].append(g_copy)
                        ga_by_cp[cp] = g_copy
                fams[fa].sort(key=lambda x: (x["cp"], x["exact_hash"], x["tile_hash"]))

    out_entries = []
    out_sections = []
    for fa in kept_order:
        s0 = len(out_entries)
        flist = fams[fa]
        out_entries.extend(flist)
        s1 = len(out_entries) - 1
        meta = sec_meta.get(fa, {"blocks": 1, "sample": ""})
        out_sections.append({
            "family": fa,
            "dim": flist[0]["dim"],
            "start": s0,
            "end": s1,
            "count": len(flist),
            "blocks": meta["blocks"],
            "sample": meta["sample"],
        })
    return out_entries, out_sections


SYNTH_INTRO_4_256_B64 = "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAYAAABccqhmAAAKK0lEQVR42u3dbW/W5hkG4LMJIaBASCMIpQgxWoQHCE3VNGnaL96n/Ylp0raq24eudK462q5qoRClEIgyGpLtg2/vcdO8x89LmuOQrKYJeWzFuk9ftu+XBAAAAAAAAAAAAAAAAAAAmFBvDXsHdV3PJTmf5GySmSRT5Uftf7fKlm3f381aVVVPnTqYwACo6/pMkutJ3klyJcmlJG0ItAHQNvLpJBudAJg6QAC8SPIoycdVVX3rFMKEBEBd1+8muZ3kZpJfJFlKcjnJfKcKmN5WBbxJstkJhP0CYCXJx0k+TPLHqqqeOI1wNGd6bPzvJ7mf5E4JgVtJbpQAWChX/z5sJlksVcNKEgEARzTVU+N/tzT+B0l+neQ3SX6b5G65DZjp8Zink3yQ5F6SW3VdX3IaYYwBUMr926VRPihhMD/E455J8l6Sa6XCAMYRAHVdL6V56HerU/7PjuDYL5dbgTmnEcZXAVxO8m653785osafDN4szDqNMN4AuFrK8SsjPPbpnm9jQAAcsvw/U8rwy2le+Y3S6zR9CN44jTCeCmChbItlG6W1JOslCIAxBMClTuOfH/Gxr5YA+MFphPFVAJfKNmprZVMBwJgCYHFM5b9bABhnANR1fS7Nw7+2q++ovU7zANBDQBhDBXAtg8E+4wiArTTjAjadRhhPAFzNaN/972TaaYQRBkAZgHO9VABvj+nYz2j8MJ4K4Eaa7r9LYyr/2yv/megJCKMLgLquL5YAuJbxvP/vHvuUAIDRVgA3S/k/rod/x61egKM0orquL5fG3wbARX9COD0VwI2yLZXy/7w/IZyCACjTft3Ydv9vLD6ckgqgbfzt67+LAgBOtgPNCtyZ9qu9+rdTfbcBspmmS27bPXcqg/f03a+BkxYAGTz4axf8aOfim0ozIOdVfjo6b7Zs52P6LjiZAVCW9nqn0/ivlgCYLlf+9TTz83+f5Hn5/zYA5tK8Ktws+xIAcMIqgO6cf+0AoG45v14a/3dJlksVkHLFXyhfn834OgwBxwiAxdLo2237Ih/raWbnWU7yOMnL8v25cjtwNs0Dww1/bjiZFUA76edOI/9el0a/nORpJwDaPgLz5feN24eTFACl3/9iuQXYaQWe6dKwV8tzgOXyHCBJLpSr/2KpElQAcMIqgMVOBbDT1X+zVABrpeF3A+CHchvQTt2lAoATGADttlu33/XSyF+UKmClfP9NmslC10oYqADghAVAu/7ebpN+vOk8A3i+LQBSvt9O3Lnlzw0nJAA69/+L2X3Yb1v+r5YAeF5V1Yvy+1Pl++0tgPn7YcJM7VP+t3P+z+8RAOvlSv+iqqrvOz97Ub7/KoNlvEzgCScoANoZf/a6/3+ZwVuA/6uqaqv8rPsQ0INAmPQAKIt+Xu5UAdkjAF5l8ABwu+4twLCeA5gZCHpuPEudANhr1Z+1cpV/XlXVT67u5XnAWpL/dG4D+jz26RhlCL0HwLVOACzsEwDPd7n6d//NsFbxMSko9BkAZeafdtDPYvae969t/Cv73CYM4yHgtACAHgOgvPprZ/y5ksGw390a/3KS5fbV3z4BoB8ATGoAlMbfTvnd3gLsdfX/LsmTsu2lLf8FAEyYM3Vdv53mVd9iBrP+LKV5+j+3x+9+neTLsu0XAG3jn+nx2Dfj1SIcLwCS3C8Nfb40/DYAFrL7+/9/J/lXks+2df7ZyZvOvs70eOxvynOFdacRjh4A90pDP5/BqL+rJQB2msJrI8lnST5N8s8D7GOjc/Xvcx2BtvELADhGADwojfNsafQLnVuAnUr2z0sAPKyq6skB9rFV9nM2/b6zf5PkdVVVa04jHC8A2tdp7Qy+uy36+V2SL8qV/5MD7mNqCFf/tgIwwAiOGQC/7Px/Wwlc2OHfvkrzxP/LJF9UVfXtAffRTg1+oedj34o3C3DsALh6wH/7IoNXf98cYh9zaV4n9r2QqA5AMKJGtFEqgHbij+eH2EfbnbjvAGirFWDIAbCVwew/h+3VdzX7jyk4itkk83VdX3IaYbgBMNXZ2hV/9lXX9bkMlhOf6fnYL6Z5bXnNaYThBsBMfrzU11KZM2A/99N0L14awrEvpFmq7NYBjwU4YgCk2/iTvJfkXpn3b7er/50kv0pyO03vwr5dLtXF3SQfOJVweIe5craN/2YGve8W6rr+Ks1w4LbDz1xpmPdLw7yTg79pOGwF8F6aEYkbdV3Pp+mk9LiqKv0DoOcAmC1X8q3y9UIJg6dppv5Kmp5+5ztVwp2yDct7aR5OXijHdi/Jk7quHyX5p16CsLe36rr+7xF+71m58q5kMPd/e0sxm8GCotcymiXBv0nTR2ElTT+FT5J8lOTPVVW9dJrh+BVA15X8eKmwzU4FMA7XO88ZnmXw1mE1yV+cZthZX73pJmlyzitJfleeP9wtE50AQwyASXOhPA+4Ff0E4NQFQNI8ILyenZc1B37mATCT/ec1BAHwM9bOdAScwgAATnEAmDMQTmkAbKTprKQjEJzCAHiUpofgstMMpysAXiV5mGYC08dOM+zs5ziO/lmSv5ftU2MBYO8A+NMeP+92722X4toqX291tu6qv7Np3r9fTTNhxyi6CO82GOihUwx7B8AfOg18e8PffouwsUsIdM2l6YF3M81Q4LsZ3rv49TSLlHzWued/Ur42HBgOEAC/3+PqP9UJga1tAZBOCHTNJ3k/zYIj62l65D0Y0vE/SvK3Uu4/jAlB4HABcIgFPg7qRZKv67pey6A77o30Pyvw8xIA/0gz7v9DpxMOZ5hvAT5Ks4DoVxnOq7jlNEuUf1r2BUxKAFRVtZXmNdzTHG4hkcNUAE/SLFNmiTCYsAogad7BD6s33svy2d7zwyQGQFVVL9JMyzWM/vjrSVbLPoAJrADahvp6CJ/7Ogb6wMQHwJuybfT4mRudzwUmOAC6/Qj6sjXC4wcBcAwzQ9rPVPpfcBQEQF/quj6b5FySs+l3gZDZ8pnnyj6ACawA5st2YQiffaHz+cAEBsDFznaSPhsEQE+NdD79jwNI+cx5AQCTGwDnR1ABmPYbJjQAZtI8sJsbwmfPlc/2JgAmNADa+QSG0Uhn8uP5CoAJC4B28pBhjNZrP1dvQJjQAFhPspZmQFDfVstnGw8AExoAq2km6hzWhCArQwoXEAA9NdLHaabu6nsw0KMM5hsAJi0Aylj9z9NM2PlRTyGwkcGU35+bDwCObhQLgzxMsli+ftb5+qhWymf+Neb9h2N5axQ7qet6Kcm9JLdz/L77q21VUVXVU6cQJjwAOkFwKcfvFLSm7AcAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAkfgfrMk1+67MhhYAAAAASUVORK5CYII="



def clean_restore_and_center_1x_tile(cell_rgba: np.ndarray, cp: int, adv_x: int, height: int) -> np.ndarray:
    """
    Cleans disconnected right-edge neighbor bleed, restores cut-off left-edge halos (at x < 0),
    and centers the glyph inside the dim x dim cell at pad_x = (dim - adv_x)//2, pad_y = (dim - height)//2.
    """
    dim = cell_rgba.shape[0]
    if cp == 0x20:
        return np.zeros_like(cell_rgba)
    work = cell_rgba.copy()
    a = work[:, :, 3]
    if a.max() == 0:
        return work
    ys0, xs0 = np.where(a > 0)
    if xs0.min() >= 2 and xs0.max() <= dim - 3:
        return work
    search_start = min(dim - 2, max(4, adv_x - 1))
    for x in range(search_start, dim):
        if a[:, x].max() == 0:
            work[:, x + 1 :, :] = 0
            break
    if adv_x + 6 < dim:
        work[:, adv_x + 6 :, :] = 0
    a = work[:, :, 3]
    if a.max() == 0:
        return work
    fringe_mask = (a > 0) & (a <= 90)
    has_halo = bool(np.any(fringe_mask))
    max_halo_w = 3 if dim >= 32 else 2
    ext = np.zeros((dim, dim + max_halo_w, 4), dtype=np.uint8)
    ext[:, max_halo_w : max_halo_w + dim] = work
    if has_halo and a[:, 0].max() > 8:
        fringe_pixels = work[fringe_mask]
        halo_rgb = np.median(fringe_pixels[:, :3], axis=0).astype(np.uint8)
        right_halo_vals = []
        for y in range(dim):
            row_a = a[y, :]
            nz_x = np.where(row_a > 120)[0]
            if len(nz_x) > 0:
                rx = int(nz_x.max())
                tail = [int(row_a[rx + d]) for d in range(1, max_halo_w + 1) if rx + d < dim and row_a[rx + d] > 0]
                if tail:
                    right_halo_vals.append(tail)
        if right_halo_vals:
            avg_tail = []
            for d in range(max_halo_w):
                vals = [t[d] for t in right_halo_vals if len(t) > d]
                avg_tail.append(int(round(np.mean(vals))) if vals else 0)
        else:
            avg_tail = [24, 14, 6] if dim >= 32 else [24, 10]
        for y in range(dim):
            a0 = int(a[y, 0])
            if a0 <= 8:
                continue
            if a0 > 120:
                for d in range(1, max_halo_w + 1):
                    ha = avg_tail[d - 1]
                    if ha > 0:
                        ext[y, max_halo_w - d, :3] = halo_rgb
                        ext[y, max_halo_w - d, 3] = ha
            else:
                cur_a = a0
                for d in range(1, max_halo_w + 1):
                    cur_a = int(round(cur_a * 0.55))
                    if cur_a >= 6:
                        ext[y, max_halo_w - d, :3] = halo_rgb
                        ext[y, max_halo_w - d, 3] = cur_a
    ys, xs = np.where(ext[:, :, 3] > 0)
    if len(xs) == 0:
        return np.zeros_like(cell_rgba)
    orig_x0 = int(xs.min()) - max_halo_w
    orig_x1 = int(xs.max()) - max_halo_w + 1
    orig_y0 = int(ys.min())
    orig_y1 = int(ys.max()) + 1
    pad_x = (dim - min(dim, adv_x)) // 2
    pad_y = (dim - min(dim, height)) // 2
    if orig_x0 + pad_x < 1:
        pad_x = 1 - orig_x0
    if orig_x1 + pad_x > dim - 1:
        pad_x = max(0 - orig_x0, (dim - 1) - orig_x1)
    if orig_y0 + pad_y < 1:
        pad_y = 1 - orig_y0
    if orig_y1 + pad_y > dim - 1:
        pad_y = max(0 - orig_y0, (dim - 1) - orig_y1)
    out = np.zeros((dim, dim, 4), dtype=np.uint8)
    for y in range(dim):
        dy = y + pad_y
        if not (0 <= dy < dim):
            continue
        for x in range(-max_halo_w, dim):
            rgba = ext[y, x + max_halo_w]
            if rgba[3] == 0:
                continue
            dx = x + pad_x
            if 0 <= dx < dim:
                out[dy, dx] = rgba
    return out


def center_hd_glyph_tile(tile_im: Image.Image, dim: int, adv_x: int, height: int) -> Image.Image:
    """Centers an HD glyph tile (e.g. 128x128 or 512x512 OstrichMods glyph) inside its cell."""
    arr = np.array(tile_im)
    a = arr[:, :, 3]
    if a.max() == 0:
        return tile_im
    th, tw = arr.shape[:2]
    scale = max(1, tw // dim)
    ys, xs = np.where(a > 0)
    x0, x1 = int(xs.min()), int(xs.max()) + 1
    y0, y1 = int(ys.min()), int(ys.max()) + 1
    if x0 >= 2 * scale and x1 <= tw - 2 * scale:
        return tile_im
    pad_x_hd = ((dim - min(dim, adv_x)) // 2) * scale
    pad_y_hd = ((dim - min(dim, height)) // 2) * scale
    if x0 + pad_x_hd < scale:
        pad_x_hd = scale - x0
    if x1 + pad_x_hd > tw - scale:
        pad_x_hd = max(0 - x0, (tw - scale) - x1)
    if y0 + pad_y_hd < scale:
        pad_y_hd = scale - y0
    if y1 + pad_y_hd > th - scale:
        pad_y_hd = max(0 - y0, (th - scale) - y1)
    out = np.zeros_like(arr)
    for sy in range(y0, y1):
        dy = sy + pad_y_hd
        if 0 <= dy < th:
            for sx in range(x0, x1):
                dx = sx + pad_x_hd
                if 0 <= dx < tw:
                    out[dy, dx] = arr[sy, sx]
    return Image.fromarray(out, mode="RGBA")


def build_master_font_atlases(
    master_font_files: dict,
    intro_letterhead: dict,
    intro_upper: dict,
    intro_lower: dict,
    out_fonts_dir: str,
) -> tuple:
    """
    Builds the Master Font Atlases in out_fonts_dir:
      - MASTER_EN_font_64x64.png + .txt (8x -> 512x512 cells, 23 Squadron Intro letterhead glyphs)
      - MASTER_EN_font_32x32.png + .txt + .json (4x -> 128x128 cells, 2048x5632, saving 75% RAM/VRAM vs 8x;
        38 Squadron Intro subtitle glyphs in Family 1C85B763AD07A4EC replaced in 4x HD; and all 131
        16x16 glyphs from MASTER_EN_font_16x16 merged at the .json level into MASTER_EN_font_32x32.json
        so no separate MASTER_EN_font_16x16.png/.txt is needed!)
    """
    if not master_font_files:
        return 0, 0, 0

    os.makedirs(out_fonts_dir, exist_ok=True)
    ci_map = {k.lower(): (k, v) for k, v in master_font_files.items()}

    # Remove legacy standalone MASTER_EN_font_16x16.png / .txt / .json if left over from an earlier run
    for old_16 in ["MASTER_EN_font_16x16.png", "MASTER_EN_font_16x16.txt", "MASTER_EN_font_16x16.json"]:
        old_16_p = os.path.join(out_fonts_dir, old_16)
        if os.path.isfile(old_16_p):
            try:
                os.remove(old_16_p)
            except Exception:
                pass

    cnt_atlases = 0
    cnt_glyphs = 0
    cnt_font_json = 0

    # 1. MASTER_EN_font_64x64.png + .txt (8x -> 512x512 centered cells)
    if "master_en_font_64x64.png" in ci_map and "master_en_font_64x64.txt" in ci_map:
        png_name, png_raw = ci_map["master_en_font_64x64.png"]
        txt_name, txt_raw = ci_map["master_en_font_64x64.txt"]
        raw_entries_64, _, _ = parse_master_txt_bytes(txt_raw, 64)
        im64_base = Image.open(io.BytesIO(png_raw)).convert("RGBA")
        if im64_base.width != 16 * 64:
            im64_1x = im64_base.resize((16 * 64, (im64_base.height // (im64_base.width // 16)) * 64), Image.Resampling.NEAREST)
        else:
            im64_1x = im64_base.copy()
        arr64_1x = np.array(im64_1x)
        for e64 in raw_entries_64:
            r, c = e64["row"], e64["col"]
            cell1x = arr64_1x[r*64:(r+1)*64, c*64:(c+1)*64]
            arr64_1x[r*64:(r+1)*64, c*64:(c+1)*64] = clean_restore_and_center_1x_tile(cell1x, e64["cp"], e64["adv_x"], e64["height"])
        canvas64 = Image.fromarray(arr64_1x, mode="RGBA").resize((im64_1x.width * 8, im64_1x.height * 8), Image.Resampling.NEAREST)

        e64_by_idx = {e64["orig_idx"]: e64 for e64 in raw_entries_64}
        for fn_low, g_idx in LETTERHEAD_64_MAP.items():
            raw_tile = intro_letterhead.get(fn_low.lower())
            if raw_tile is not None:
                tile512 = process_intro_glyph_tile(raw_tile, 255)
                if g_idx in e64_by_idx:
                    tile512 = center_hd_glyph_tile(tile512, 64, e64_by_idx[g_idx]["adv_x"], e64_by_idx[g_idx]["height"])
                col, row = g_idx % 16, g_idx // 16
                canvas64.paste(tile512, (col * 512, row * 512))
                cnt_glyphs += 1

        canvas64.save(os.path.join(out_fonts_dir, png_name), format="PNG", compress_level=4)
        txt64_str = txt_raw.decode("utf-8", errors="replace")
        if "CenteredCells:" not in txt64_str:
            txt64_str = txt64_str.replace("Grid Columns:          16\n", "Grid Columns:          16\nCenteredCells:         1\n", 1)
        with open(os.path.join(out_fonts_dir, txt_name), "w", encoding="utf-8", newline="\n") as tf:
            tf.write(txt64_str)
        cnt_atlases += 1

    # 2. MASTER_EN_font_32x32.png + .txt (4x -> 128x128 cells, 2048px wide) + MASTER_EN_font_32x32.json (merging 16x16)
    if "master_en_font_32x32.png" in ci_map and "master_en_font_32x32.txt" in ci_map:
        png32_name, png32_raw = ci_map["master_en_font_32x32.png"]
        txt32_name, txt32_raw = ci_map["master_en_font_32x32.txt"]
        raw_entries_32, sec_meta_32, samples_32 = parse_master_txt_bytes(txt32_raw, 32)
        raw_entries_32 = [e for e in raw_entries_32 if e["dim"] == 32]
        im32_src = Image.open(io.BytesIO(png32_raw)).convert("RGBA")
        cols = 16
        cell_hd = 128  # 4x HD cell size (2048px wide)
        cell32_w = im32_src.width // cols

        # Compute per-family standard height (std_h) across 32x32 families
        fam32_std_h = {}
        by_f32_tmp = {}
        for e in raw_entries_32:
            by_f32_tmp.setdefault(e["family"], []).append(e)
        for fh, elist in by_f32_tmp.items():
            alnum_h = [x["height"] for x in elist if chr(x["cp"]).isalnum() and x["height"] > 0]
            all_h = [x["height"] for x in elist if x["height"] > 0]
            src_h = alnum_h if alnum_h else all_h
            fam32_std_h[fh] = max(set(src_h), key=src_h.count) if src_h else 32

        out_png32_path = os.path.join(out_fonts_dir, png32_name)

        ostrich_by_cp = {}
        for fn_low, cps in INTRO_SUB_32_UPPER_MAP.items():
            raw_tile = intro_upper.get(fn_low.lower())
            if raw_tile is not None:
                tile256 = process_intro_glyph_tile(raw_tile, 217)
                tile128 = resize_premultiplied_lanczos(tile256, 128, 128)
                for cp_val in cps:
                    ostrich_by_cp[cp_val] = tile128
        for fn_low, cps in INTRO_SUB_32_LOWER_MAP.items():
            raw_tile = intro_lower.get(fn_low.lower())
            if raw_tile is not None:
                tile256 = process_intro_glyph_tile(raw_tile, 217)
                tile128 = resize_premultiplied_lanczos(tile256, 128, 128)
                for cp_val in cps:
                    ostrich_by_cp[cp_val] = tile128

        if intro_upper or intro_lower:
            synth_4_256 = Image.open(io.BytesIO(base64.b64decode(SYNTH_INTRO_4_256_B64))).convert("RGBA")
            ostrich_by_cp[0x0034] = resize_premultiplied_lanczos(synth_4_256, 128, 128)

        if cell32_w >= 128:
            # Source MASTER_EN_font_32x32.png is ALREADY in final HD resolution (e.g. 2048x5632)!
            # Preserve all existing HD glyphs directly without downscaling to 1x or re-upscaling,
            # and overlay the 38 OstrichMods Squadron Intro glyphs into Family=1C85B763AD07A4EC (#601..#638)!
            if cell32_w != cell_hd:
                target_rows = im32_src.height // cell32_w
                canvas32 = resize_premultiplied_lanczos(im32_src, cols * cell_hd, target_rows * cell_hd)
            else:
                canvas32 = im32_src.copy()

            dedup_32 = raw_entries_32
            for e in dedup_32:
                std_h_val = fam32_std_h.get(e["family"], e["height"])
                e["std_h"] = std_h_val
                if e["family"] == "1C85B763AD07A4EC":
                    if e["cp"] in ostrich_by_cp:
                        cell_im = center_hd_glyph_tile(ostrich_by_cp[e["cp"]], 32, e["adv_x"], std_h_val)
                        canvas32.paste(cell_im, (e["col"] * cell_hd, e["row"] * cell_hd))
                        cnt_glyphs += 1
                    else:
                        cnt_glyphs += 1

            canvas32.save(out_png32_path, format="PNG", compress_level=4)
            txt32_str = txt32_raw.decode("utf-8", errors="replace")
            if "CenteredCells:" not in txt32_str:
                txt32_str = txt32_str.replace("Grid Columns:          16\n", "Grid Columns:          16\nCenteredCells:         1\n", 1)
            with open(os.path.join(out_fonts_dir, txt32_name), "w", encoding="utf-8", newline="\n") as f:
                f.write(txt32_str)
            cnt_atlases += 1
        else:
            # Legacy 1x (512px wide) source dump: clean, center with family std_h, deduplicate, and build 4x HD master
            if cell32_w != 32:
                im32_src = im32_src.resize((16 * 32, (im32_src.height // cell32_w) * 32), Image.Resampling.NEAREST)
            src_32_arr = np.array(im32_src)

            ostrich_by_cp = {}
            for fn_low, cps in INTRO_SUB_32_UPPER_MAP.items():
                raw_tile = intro_upper.get(fn_low.lower())
                if raw_tile is not None:
                    tile256 = process_intro_glyph_tile(raw_tile, 217)
                    tile128 = resize_premultiplied_lanczos(tile256, 128, 128)
                    for cp_val in cps:
                        ostrich_by_cp[cp_val] = tile128
            for fn_low, cps in INTRO_SUB_32_LOWER_MAP.items():
                raw_tile = intro_lower.get(fn_low.lower())
                if raw_tile is not None:
                    tile256 = process_intro_glyph_tile(raw_tile, 217)
                    tile128 = resize_premultiplied_lanczos(tile256, 128, 128)
                    for cp_val in cps:
                        ostrich_by_cp[cp_val] = tile128

            synth_4_256 = Image.open(io.BytesIO(base64.b64decode(SYNTH_INTRO_4_256_B64))).convert("RGBA")
            ostrich_by_cp[0x0034] = resize_premultiplied_lanczos(synth_4_256, 128, 128)

            for e in raw_entries_32:
                g = e["orig_idx"]
                sx0, sy0 = (g % cols) * 32, (g // cols) * 32
                e["tile_1x"] = src_32_arr[sy0 : sy0 + 32, sx0 : sx0 + 32]
                if e["family"] == "1C85B763AD07A4EC" and e["cp"] in ostrich_by_cp:
                    e["ostrich_im"] = ostrich_by_cp[e["cp"]]

            dedup_32, secs_32 = deduplicate_visual_families(raw_entries_32, sec_meta_32)
            total_32 = len(dedup_32)
            rows_32 = (total_32 + cols - 1) // cols
            canvas32 = Image.new("RGBA", (cols * cell_hd, rows_32 * cell_hd), (0, 0, 0, 0))

            for new_idx, e in enumerate(dedup_32):
                e["idx"] = new_idx
                e["col"] = new_idx % cols
                e["row"] = new_idx // cols
                std_h_val = fam32_std_h.get(e["family"], e["height"])
                e["std_h"] = std_h_val
                if e.get("ostrich_im") is not None:
                    cell_im = center_hd_glyph_tile(e["ostrich_im"], 32, e["adv_x"], std_h_val)
                    cnt_glyphs += 1
                else:
                    cent_1x = clean_restore_and_center_1x_tile(e["tile_1x"], e["cp"], e["adv_x"], std_h_val)
                    cell_im = Image.fromarray(cent_1x, mode="RGBA").resize((cell_hd, cell_hd), Image.Resampling.NEAREST)
                canvas32.paste(cell_im, ((new_idx % cols) * cell_hd, (new_idx // cols) * cell_hd))

            canvas32.save(out_png32_path, format="PNG", compress_level=4)
            total_blocks_32 = sum(s["blocks"] for s in secs_32)
            with open(os.path.join(out_fonts_dir, txt32_name), "w", encoding="utf-8", newline="\n") as f:
                f.write("====================================================================\n")
                f.write(" Master Font Atlas [EN] — Все 32x32 глифы языка в одном файле (4x, без дубликатов)\n")
                f.write("====================================================================\n")
                f.write("Master Atlas Name:     MASTER_EN_font_32x32\n")
                f.write("Language:              EN\n")
                f.write("Glyph Dimension:       32x32\n")
                f.write("Grid Columns:          16\n")
                f.write("CenteredCells:         1\n")
                f.write(f"Collected Glyphs:      {total_32} (32x32, 16 columns)\n")
                f.write(f"Member Font Blocks:    {total_blocks_32}\n")
                f.write("To replace all 32x32 (and linked 16x16) fonts in-game:\n")
                f.write("  Copy edited MASTER_EN_font_32x32.png, .txt, AND .json into mods\\textures\\\n")
                f.write("--------------------------------------------------------------------\n")
                f.write("TYPEFACE SECTIONS IN THIS MASTER ATLAS (Секции гарнитур по порядку):\n")
                for s in secs_32:
                    r0, r1 = s["start"] // cols, s["end"] // cols
                    f.write(
                        f"  * Glyphs #{s['start']:03d}..#{s['end']:03d} (rows {r0}..{r1}): "
                        f"Family={s['family']} ({s['count']} уникальных глифов, "
                        f"{s['blocks']} блоков) | Пример: \"{s['sample']}\"\n"
                    )
                f.write("--------------------------------------------------------------------\n")
                f.write("GLYPH METRICS (Grid 16 columns):\n")
                for e in dedup_32:
                    alts = sorted(h for h in e.get("alt_hashes", set()) if h not in (e["tile_hash"], e["exact_hash"]))
                    alt_str = f" | AltHashes={','.join(alts)}" if alts else ""
                    f.write(
                        f"  Glyph #{e['idx']:03d} (col={e['col']}, row={e['row']}): "
                        f"U+{e['cp']:04X} '{e['ch']}' | adv_x={e['adv_x']}, height={e['height']} | "
                        f"TileHash={e['tile_hash']} | ExactHash={e['exact_hash']} | "
                        f"Family={e['family']}{alt_str}\n"
                    )
            cnt_atlases += 1

        # If cells #701..#703 exist in canvas32 (Subtitle Brackets GIM BB10DED7AB6393C6),
        # also save standalone 4x HD GIM replacement `subtitle_brackets_BB10DED7AB6393C6.png` in out_fonts_dir!
        if canvas32.height >= 44 * cell_hd:
            c32_arr = np.array(canvas32)
            gim_sb = np.zeros((cell_hd, cell_hd, 4), dtype=np.uint8)
            sc = cell_hd // 32
            # #701 (col=13, row=43): '<' (11x25 at 10,3 -> 0,0)
            c701 = c32_arr[43*cell_hd:44*cell_hd, 13*cell_hd:14*cell_hd]
            gim_sb[0:25*sc, 0:11*sc] = c701[3*sc:28*sc, 10*sc:21*sc]
            # #702 (col=14, row=43): '[' (9x25 at 11,3 -> 11,0)
            c702 = c32_arr[43*cell_hd:44*cell_hd, 14*cell_hd:15*cell_hd]
            gim_sb[0:25*sc, 11*sc:20*sc] = c702[3*sc:28*sc, 11*sc:20*sc]
            # #703 (col=15, row=43): '-' (15x7 at 8,13 -> 0,25)
            c703 = c32_arr[43*cell_hd:44*cell_hd, 15*cell_hd:16*cell_hd]
            gim_sb[25*sc:32*sc, 0:15*sc] = c703[13*sc:20*sc, 8*sc:23*sc]
            if gim_sb[:, :, 3].max() > 0:
                sb_path = os.path.join(out_fonts_dir, "subtitle_brackets_BB10DED7AB6393C6.png")
                Image.fromarray(gim_sb, mode="RGBA").save(sb_path, format="PNG", compress_level=4)

        # 3. Combine MASTER_EN_font_16x16 with MASTER_EN_font_32x32 at the .json level (MASTER_EN_font_32x32.json)
        if "master_en_font_16x16.png" in ci_map and "master_en_font_16x16.txt" in ci_map:
            _, png16_raw = ci_map["master_en_font_16x16.png"]
            _, txt16_raw = ci_map["master_en_font_16x16.txt"]
            raw_entries_16, sec_meta_16, _ = parse_master_txt_bytes(txt16_raw, 16)
            im16_src = Image.open(io.BytesIO(png16_raw)).convert("RGBA")
            cell16_w = im16_src.width // 16
            if cell16_w != 16:
                im16_src = im16_src.resize((16 * 16, (im16_src.height // cell16_w) * 16), Image.Resampling.NEAREST)
            src_16_arr = np.array(im16_src)

            for e16 in raw_entries_16:
                g = e16["orig_idx"]
                sx0, sy0 = (g % cols) * 16, (g // cols) * 16
                e16["tile_1x"] = src_16_arr[sy0 : sy0 + 16, sx0 : sx0 + 16]

            dedup_16, _ = deduplicate_visual_families(raw_entries_16, sec_meta_16)

            by_fam32 = {}
            for g32 in dedup_32:
                by_fam32.setdefault(g32["family"], {})[g32["cp"]] = g32

            # Priority of 32x32 Eurostile donor families for each 16x16 family:
            # - A9910AD20F17AD1A ("CONTINUE :OFF", glowing Eurostile) -> 10C596C6BDDD728C (32x32 glowing Eurostile), fallback 580E0574D95B2442 / B1A1FCD42EF604EC / B986D279B830A42E
            # - 12C5663260CB8DB3 ("MAIN MENU") & 0FC97B55D270F000 ("NEW GAME") -> 580E0574D95B2442 (Eurostile), fallback B1A1FCD42EF604EC / B986D279B830A42E
            donor_priority = {
                "A9910AD20F17AD1A": ["B1A1FCD42EF604EC", "10C596C6BDDD728C", "580E0574D95B2442", "B986D279B830A42E"],
                "12C5663260CB8DB3": ["580E0574D95B2442", "B1A1FCD42EF604EC", "B986D279B830A42E"],
                "0FC97B55D270F000": ["580E0574D95B2442", "B1A1FCD42EF604EC", "B986D279B830A42E"],
            }
            default_prio = ["580E0574D95B2442", "B1A1FCD42EF604EC", "10C596C6BDDD728C", "B986D279B830A42E"]

            fam16_std_h = {}
            by_f16_tmp = {}
            for e16 in dedup_16:
                by_f16_tmp.setdefault(e16["family"], []).append(e16)
            for fh16, elist16 in by_f16_tmp.items():
                alnum_h16 = [x["height"] for x in elist16 if chr(x["cp"]).isalnum() and x["height"] > 0]
                all_h16 = [x["height"] for x in elist16 if x["height"] > 0]
                src_h16 = alnum_h16 if alnum_h16 else all_h16
                fam16_std_h[fh16] = max(set(src_h16), key=src_h16.count) if src_h16 else 13

            glyphs_16_json = []
            for idx16, e16 in enumerate(dedup_16):
                cp = e16["cp"]
                fam16 = e16["family"]
                donor = None
                for dfam in donor_priority.get(fam16, default_prio):
                    if cp in by_fam32.get(dfam, {}):
                        donor = by_fam32[dfam][cp]
                        break
                if donor is None:
                    for dfam, cmap in by_fam32.items():
                        if cp in cmap:
                            donor = cmap[cp]
                            break
                if donor is None:
                    continue

                src_dim = 32 if donor["height"] >= 28 else 24
                alts = sorted(h for h in e16.get("alt_hashes", set()) if h not in (e16["tile_hash"], e16["exact_hash"]))
                glyphs_16_json.append({
                    "index_16": idx16,
                    "donor_cell_32": donor["idx"],
                    "char_code": f"U+{cp:04X}",
                    "adv16": e16["adv_x"],
                    "h16": e16["height"],
                    "std_h16": fam16_std_h.get(fam16, e16["height"]),
                    "src_dim": src_dim,
                    "tile_hash": e16["tile_hash"],
                    "exact_hash": e16["exact_hash"],
                    "family_hash": fam16,
                    "alt_hashes": alts,
                })

            write_companion_json(
                out_png32_path,
                {
                    "comment": "Combined MASTER_EN_font_32x32 (4x, 128x128 centered cells) + MASTER_EN_font_16x16 + Subtitle Brackets GIM BB10DED7AB6393C6",
                    "upscale_factor": 4,
                    "cell_size": cell_hd,
                    "centered_cells": True,
                    "merged_16x16_master": "MASTER_EN_font_16x16",
                    "subtitle_brackets_gim": {
                        "gim_hash": "BB10DED7AB6393C6",
                        "cells": [
                            {"cell": 701, "symbol": "<", "src_1x": [10, 3, 11, 25], "dst_1x": [0, 0, 11, 25]},
                            {"cell": 702, "symbol": "[", "src_1x": [11, 3, 9, 25], "dst_1x": [11, 0, 9, 25]},
                            {"cell": 703, "symbol": "-", "src_1x": [8, 13, 15, 7], "dst_1x": [0, 25, 15, 7]},
                        ],
                    },
                    "hashes": ["MASTER_EN_font_32x32", "MASTER_EN_font_16x16"],
                    "glyphs_16x16": glyphs_16_json,
                },
            )
            cnt_font_json += 1

    return cnt_atlases, cnt_glyphs, cnt_font_json


def prompt_post_conversion_cleanup(
    input_path: str,
    tc_zip_path: str,
    spw_dir: str,
    script_path: str,
    no_prompt: bool,
) -> int:
    if no_prompt or not sys.stdin or not sys.stdin.isatty():
        return 0

    archives_to_clean = []
    if input_path and os.path.isfile(input_path) and input_path.lower().endswith(".zip"):
        archives_to_clean.append(os.path.abspath(input_path))
    if tc_zip_path and os.path.isfile(tc_zip_path) and tc_zip_path.lower().endswith(".zip"):
        tc_abs = os.path.abspath(tc_zip_path)
        if tc_abs not in archives_to_clean:
            archives_to_clean.append(tc_abs)

    has_spw_dir = bool(spw_dir and os.path.isdir(spw_dir))
    bat_abs = os.path.abspath(script_path) if script_path and os.path.isfile(script_path) else ""

    print("")
    print("Cleanup Options (Очистка остаточных файлов после конвертации):")
    print("  [1] Keep all archives and this .bat converter (Оставить всё как есть) [Default]")
    print("  [2] Delete residual archives only (Удалить только исходные .zip архивы и папку spw)")
    print("  [3] Delete BOTH residual archives AND this .bat converter (Удалить архивы И этот .bat файл)")
    try:
        ans = input("Select option / Выберите вариант [1/2/3 or y/N]: ").strip().lower()
    except EOFError:
        return 0

    delete_archives = ans in ("2", "3", "y", "yes", "д", "да")
    delete_self = ans in ("3", "y", "yes", "д", "да")

    if delete_archives:
        for zp in archives_to_clean:
            try:
                os.remove(zp)
                print(f"[*] Deleted archive: {zp}")
            except Exception as exc:
                print(f"[!] Could not delete {zp}: {exc}")
        if has_spw_dir:
            try:
                shutil.rmtree(spw_dir)
                print(f"[*] Removed temporary source folder: {spw_dir}")
            except Exception as exc:
                print(f"[!] Could not remove {spw_dir}: {exc}")

    if delete_self and bat_abs:
        print(f"[*] Converter '{os.path.basename(bat_abs)}' will be removed upon exit.")
        try:
            input("Press Enter to finish and remove the .bat file...")
        except EOFError:
            pass
        return 99

    return 0


def main() -> int:
    print("=" * 78)
    print(f" ACZ OstrichMods PCSX2 HUD & Font Converter ({PLUGIN_VERSION})")
    print("=" * 78)

    script_path = os.path.abspath(__file__)
    script_dir = os.path.dirname(script_path)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a.lower() for a in sys.argv[1:] if a.startswith("--")}
    align_emblems = "--raw-emblem-aspect" not in flags
    no_prompt = "--no-prompt" in flags

    input_path = args[0] if len(args) >= 1 else ""
    tc_zip_path = args[2] if len(args) >= 3 else ""

    # If the user dragged & dropped `custom textures.zip` (or a folder with `For OstrichMods`) as arg[0],
    # treat it as tc_zip_path and look for OstrichMods zip automatically!
    if input_path and os.path.exists(input_path):
        b_in = os.path.basename(os.path.normpath(input_path)).lower()
        if "custom" in b_in or "converter" in b_in or os.path.isdir(os.path.join(input_path, "For OstrichMods")):
            if not tc_zip_path:
                tc_zip_path = input_path
            input_path = find_default_input_source(script_dir)
    else:
        tc_zip_path = find_texture_converter_zip(script_dir, "")
        input_path = find_default_input_source(script_dir)

    if not tc_zip_path:
        tc_zip_path = find_texture_converter_zip(script_dir, input_path)

    if (not input_path or not os.path.exists(input_path)) and (not tc_zip_path or not os.path.exists(tc_zip_path)):
        print("[ERROR] Could not find 'custom textures.zip' or 'actrilogy-hud-upgrade-v1.2.1.zip'.")
        print("Usage:")
        print("  1. Place 'custom textures.zip' (and 'actrilogy-hud-upgrade-v1.2.1.zip') in 'mods\' and run this .bat")
        print("  2. Or drag and drop 'custom textures.zip' onto ACZ_Textures_installer.bat")
        print("  3. Or run: ACZ_Textures_installer.bat [ostrich_zip_or_custom_zip] [output_dir] [custom_textures_zip]")
        return 1

    output_dir = args[1] if len(args) >= 2 else resolve_default_output_dir(script_dir)
    mods_textures_dir = resolve_mods_textures_root(output_dir, script_dir)

    out_air_dir = os.path.join(output_dir, "icons-aircraft")
    out_weap_dir = os.path.join(output_dir, "icons-weapons")
    out_spw_dir = os.path.join(out_weap_dir, "spw")
    out_atl_dir = os.path.join(out_weap_dir, "atlases")
    out_diag_dir = os.path.join(out_weap_dir, "descriptions")
    out_hangar_weap_dir = os.path.join(out_weap_dir, "hangar-weapons")
    out_ux_dir = os.path.join(output_dir, "ux")
    out_emb_dir = os.path.join(output_dir, "ux", "intro-squadron", "emblems")
    out_fonts_dir = os.path.join(output_dir, "ux", "intro-squadron", "fonts")
    legacy_atl_dir = os.path.join(output_dir, "atlases")

    print(f"[*] OstrichMods Source : {os.path.abspath(input_path) if input_path and os.path.exists(input_path) else '(not provided, installing custom textures only)'}")
    print(f"[*] Custom Archive     : {os.path.abspath(tc_zip_path) if tc_zip_path else '(not found, checking local folders)'}")
    print(f"[*] OstrichMods Output : {os.path.abspath(output_dir)}")
    print(f"[*] Direct Mods Output : {os.path.abspath(mods_textures_dir)}")
    print("-" * 78)

    src_files, spw_files, master_font_files = load_source_files(input_path, tc_zip_path, out_spw_dir)
    cnt_direct_tex = install_direct_textures_from_sources(tc_zip_path, script_dir, mods_textures_dir)
    if not src_files and not spw_files and not master_font_files and cnt_direct_tex == 0:
        print(f"[ERROR] No convertible files found in: {input_path or tc_zip_path}")
        return 1

    os.makedirs(out_air_dir, exist_ok=True)
    os.makedirs(out_weap_dir, exist_ok=True)
    os.makedirs(out_ux_dir, exist_ok=True)
    os.makedirs(out_emb_dir, exist_ok=True)

    # Clean up legacy mixed 'mods/textures/ostrichmods/atlases' directory from v2..v4
    if os.path.isdir(legacy_atl_dir):
        try:
            shutil.rmtree(legacy_atl_dir)
            print(f"[*] Removed legacy mixed folder: {legacy_atl_dir}")
        except Exception:
            pass

    # Remove redundant or legacy .json / duplicate UX files if left over from an earlier run
    for old_json in [
        os.path.join(out_air_dir, "f15c_f15e_7BBFF633CB7C5D32.json"),
        os.path.join(out_weap_dir, "f01a_f05e_j35j_spw2_3B97DB2F5EB61035.json"),
        os.path.join(out_ux_dir, "000.json"),
        os.path.join(out_ux_dir, "001.json"),
        os.path.join(out_ux_dir, "d00645d18395b834.png"),
        os.path.join(out_ux_dir, "d00645d18395b834.json"),
    ]:
        if os.path.isfile(old_json):
            try:
                os.remove(old_json)
            except Exception:
                pass

    cnt_air = 0
    cnt_weap = 0
    cnt_ux = 0
    cnt_emb = 0
    cnt_atl = 0
    cnt_diag = 0
    cnt_hangar_weap = 0
    cnt_json = 0

    processed_weap_imgs = {}
    intro_letterhead = {}
    intro_upper = {}
    intro_lower = {}

    for rel_path, png_bytes in sorted(src_files.items()):
        parts = rel_path.split("/")
        folder = parts[0].lower()
        filename = parts[-1]

        # 1. icons-aircraft (crop 0,0,288,288 from 384x384 + restore PS2 alpha & interior brightness)
        if folder == "icons-aircraft" and len(parts) == 2:
            if filename == "e6d874b58048e40c-d6599b7ebe3fba5d-00001993.png":
                continue
            target_name = AIRCRAFT_MAP.get(filename)
            if target_name:
                out_im = process_aircraft_icon(png_bytes)
                out_p = os.path.join(out_air_dir, target_name)
                out_im.save(out_p, format="PNG", compress_level=4)
                cnt_air += 1

        # 2. icons-weapons (256x256 + unscale PS2 alpha; emit companion .json only when multiple distinct hashes exist)
        elif folder == "icons-weapons" and len(parts) == 2:
            target_names = WEAPONS_MAP.get(filename)
            if target_names:
                out_im = process_weapon_icon(png_bytes)
                processed_weap_imgs[filename.lower()] = out_im
                if len(target_names) == 1:
                    out_p = os.path.join(out_weap_dir, target_names[0])
                    out_im.save(out_p, format="PNG", compress_level=4)
                    cnt_weap += 1
                else:
                    hashes = [tn.split("_")[-1].split(".")[0] for tn in target_names]
                    shared_name = f"typn_rm01_spw_xlaa_{hashes[0]}.png"
                    out_p = os.path.join(out_weap_dir, shared_name)
                    out_im.save(out_p, format="PNG", compress_level=4)
                    write_companion_json(
                        out_p,
                        {
                            "comment": "Shared XLAA weapon icon (Typhoon spw2 & Rafale M spw3)",
                            "hashes": hashes,
                        },
                    )
                    cnt_weap += 1
                    cnt_json += 1

        # 3. ux (top-level HUD atlases + intro-squadron/emblems + intro-squadron/letterhead, upper-num, lower)
        elif folder == "ux":
            if len(parts) == 2:
                out_name = UX_SPECS.get(filename)
                if out_name:
                    out_im = process_ux_atlas(png_bytes)
                    out_p = os.path.join(out_ux_dir, out_name)
                    out_im.save(out_p, format="PNG", compress_level=4)
                    cnt_ux += 1
            elif len(parts) >= 3 and parts[1].lower() == "intro-squadron":
                sub = parts[2].lower()
                if sub == "emblems" and len(parts) == 4:
                    emb_info = EMBLEMS_MAP.get(filename)
                    if emb_info:
                        target_name, src_bbox, dst_bbox = emb_info
                        out_im = process_squadron_emblem(png_bytes, src_bbox, dst_bbox, align_bbox=align_emblems)
                        out_im.save(os.path.join(out_emb_dir, target_name), format="PNG", compress_level=4)
                        cnt_emb += 1
                elif sub == "letterhead" and len(parts) == 4:
                    intro_letterhead[filename.lower()] = png_bytes
                elif sub == "upper-num" and len(parts) == 4:
                    intro_upper[filename.lower()] = png_bytes
                elif sub == "lower" and len(parts) == 4:
                    intro_lower[filename.lower()] = png_bytes

    # 3B. Assemble HD Master Font Atlases (8x MASTER_EN_font_64x64, 4x MASTER_EN_font_32x32 + .json merging 16x16)
    cnt_font_atlases, cnt_font_glyphs, cnt_font_json = build_master_font_atlases(
        master_font_files,
        intro_letterhead,
        intro_upper,
        intro_lower,
        out_fonts_dir,
    )
    cnt_json += cnt_font_json

    # Check if 3b97db2f5eb61035.png is present in texture converter.zip / spw_files
    ugbs_hd_im = None
    for cand_key in ["3b97db2f5eb61035.png", "f01a_spw2_3b97db2f5eb61035.png", "ugbs.png"]:
        if cand_key in spw_files:
            ugbs_hd_im = process_custom_ugbs_icon(spw_files[cand_key])
            out_p = os.path.join(out_weap_dir, "f01a_f05e_j35j_spw2_3B97DB2F5EB61035.png")
            ugbs_hd_im.save(out_p, format="PNG", compress_level=4)
            cnt_weap += 1
            break
    if ugbs_hd_im is not None:
        processed_weap_imgs["3b97db2f5eb61035.png"] = ugbs_hd_im
    else:
        fb_ugbs = Image.open(io.BytesIO(base64.b64decode(FALLBACK_UGBS_1X_B64))).convert("RGBA")
        processed_weap_imgs["3b97db2f5eb61035.png"] = resize_premultiplied_lanczos(fb_ugbs, 256, 256)

    # 4. Assemble Hangar SPW textures into 3 dedicated directories inside mods\textures\ostrichmods\icons-weapons\:
    #    - icons-weapons\atlases        : 36 composite 2048x1024 GIM Hangar SPW Atlases (without .json)
    #    - icons-weapons\descriptions   : 19 TIM2 512x512 [STATISTICS] weapon trajectory description diagrams + .json
    #    - icons-weapons\hangar-weapons : 60 TIM2 320x320 Hangar special weapon icons + .json
    has_ac5_descs = any("00001dd3" in k for k in spw_files)
    if has_ac5_descs:
        os.makedirs(out_atl_dir, exist_ok=True)
        os.makedirs(out_diag_dir, exist_ok=True)
        os.makedirs(out_hangar_weap_dir, exist_ok=True)

        for fn in os.listdir(out_atl_dir):
            if fn.startswith("hangar_weap_") or fn.startswith("hangar_diag_") or fn.lower().endswith(".json"):
                try:
                    os.remove(os.path.join(out_atl_dir, fn))
                except Exception:
                    pass

        processed_desc_clusters = {}
        for cid, (desc_fn, sbox, dbox) in DESC_CLUSTERS_MAP.items():
            key_low = desc_fn.lower()
            if key_low in spw_files:
                processed_desc_clusters[cid] = (
                    process_ac5_description_diagram(spw_files[key_low], sbox, dbox),
                    dbox,
                )
            else:
                fb_mpbm = Image.open(io.BytesIO(base64.b64decode(FALLBACK_MPBM_DESC_1X_B64))).convert("RGBA")
                dx0, dy0, dx1, dy1 = dbox
                crop_1x = fb_mpbm.crop((dx0 // 8, dy0 // 8, dx1 // 8, dy1 // 8))
                processed_desc_clusters[cid] = (
                    resize_premultiplied_lanczos(crop_1x, dx1 - dx0, dy1 - dy0),
                    dbox,
                )

        boosted_weap_imgs = {
            k: boost_weapon_icon_for_hangar_atlas(v) for k, v in processed_weap_imgs.items()
        }

        # 4A. 36 Composite Hangar SPW Atlases (2048x1024, without .json) -> icons-weapons\atlases
        for atlas_name, slots_spec in sorted(HANGAR_ATLAS_MAP.items()):
            canvas = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))
            for slot_i, (weap_fn, desc_cid) in enumerate(slots_spec):
                wim = boosted_weap_imgs.get(weap_fn.lower())
                if wim is not None:
                    canvas.paste(wim, (slot_i * 320 + 32, 32))
                dim_entry = processed_desc_clusters.get(desc_cid)
                if dim_entry is not None:
                    dim_im, dbox = dim_entry
                    canvas.paste(dim_im, (slot_i * 640 + dbox[0], 512 + dbox[1]))
            canvas.save(os.path.join(out_atl_dir, atlas_name), format="PNG", compress_level=4)
            cnt_atl += 1

        # 4B. 60 Hangar TIM2 320x320 Special Weapon Icons (+ .json) -> icons-weapons\hangar-weapons
        for wspec in HANGAR_TIM2_WEAP_SPECS:
            base_im = processed_weap_imgs.get(wspec["weap_fn"].lower())
            if base_im is None:
                continue
            w_arr = np.array(base_im)
            a_ch = w_arr[:, :, 3]
            ys, xs = np.where(a_ch > 8)
            if len(xs) > 0:
                crop_a = Image.fromarray(a_ch[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1], mode="L")
            else:
                crop_a = Image.fromarray(a_ch, mode="L")
            dx0, dy0, dx1, dy1 = wspec["dst_320"]
            tw, th = max(1, dx1 - dx0), max(1, dy1 - dy0)
            res_a = np.array(crop_a.resize((tw, th), Image.Resampling.LANCZOS), dtype=np.float32)
            a_tim2 = np.where(
                res_a <= 62.0,
                res_a * (108.0 / 62.0),
                108.0 + (res_a - 62.0) * (255.0 - 108.0) / (255.0 - 62.0),
            )
            a_u8 = np.clip(np.round(a_tim2), 0, 255).astype(np.uint8)
            tile_320 = np.zeros((320, 320, 4), dtype=np.uint8)
            tile_320[dy0:dy1, dx0:dx1, 3] = a_u8
            mask = tile_320[:, :, 3] > 0
            tile_320[mask, :3] = 254
            out_p = os.path.join(out_hangar_weap_dir, wspec["out_name"])
            Image.fromarray(tile_320, mode="RGBA").save(out_p, format="PNG", compress_level=4)
            write_companion_json(
                out_p,
                {
                    "comment": f"Hangar TIM2 Special Weapon Icon ({wspec['out_name']})",
                    "width": 40,
                    "height": 40,
                    "hashes": wspec["hashes"],
                },
            )
            cnt_hangar_weap += 1
            cnt_json += 1

        # 4C. 19 Hangar TIM2 512x512 [STATISTICS] Description Diagrams (+ .json) -> icons-weapons\descriptions
        for dspec in HANGAR_TIM2_DIAG_SPECS:
            cid = dspec["cid"]
            desc_fn, sbox, _ = DESC_CLUSTERS_MAP[cid]
            key_low = desc_fn.lower()
            dx0, dy0, dx1, dy1 = dspec["dst_512"]
            tw, th = max(1, dx1 - dx0), max(1, dy1 - dy0)
            if key_low in spw_files:
                im_spw = Image.open(io.BytesIO(spw_files[key_low])).convert("RGBA")
                a_spw = np.array(im_spw)[:, :, 3].astype(np.float32)
                nz = a_spw[a_spw > 0]
                if len(nz) > 0:
                    p80 = float(np.percentile(nz, 80))
                    if p80 > 0:
                        a_spw = np.minimum(255.0, a_spw * (255.0 / p80) * 1.15)
                a_im = Image.fromarray(np.clip(np.round(a_spw), 0, 255).astype(np.uint8), mode="L")
                sx0, sy0, sx1, sy1 = sbox
                cropped_a = a_im.crop((sx0, sy0, sx1, sy1)) if a_im.size == (512, 512) else a_im
                res_a = np.array(cropped_a.resize((tw, th), Image.Resampling.LANCZOS), dtype=np.float32)
                res_a = np.minimum(255.0, res_a * 1.25)
                a_u8 = np.clip(np.round(res_a), 0, 255).astype(np.uint8)
            else:
                dim_entry = processed_desc_clusters.get(cid)
                if dim_entry is None:
                    continue
                dim_im, _ = dim_entry
                a_u8 = np.array(dim_im.resize((tw, th), Image.Resampling.LANCZOS))[:, :, 3]
            tile_512 = np.zeros((512, 512, 4), dtype=np.uint8)
            tile_512[dy0:dy1, dx0:dx1, 3] = a_u8
            mask = tile_512[:, :, 3] > 0
            tile_512[mask, :3] = 254
            out_p = os.path.join(out_diag_dir, dspec["out_name"])
            Image.fromarray(tile_512, mode="RGBA").save(out_p, format="PNG", compress_level=4)
            write_companion_json(
                out_p,
                {
                    "comment": f"Hangar TIM2 [STATISTICS] Weapon Trajectory Diagram (Cluster #{cid:02d})",
                    "width": 64,
                    "height": 64,
                    "hashes": dspec["hashes"],
                },
            )
            cnt_diag += 1
            cnt_json += 1

    total_written = cnt_air + cnt_weap + cnt_ux + cnt_emb + cnt_font_atlases + cnt_atl + cnt_diag + cnt_hangar_weap + cnt_direct_tex
    if cnt_direct_tex > 0:
        print(f"[+] Installed Direct Textures  : {cnt_direct_tex:3d} files -> {mods_textures_dir}")
    print(f"[+] Converted Aircraft Icons   : {cnt_air:3d} files -> {out_air_dir}")
    print(f"[+] Converted HUD Weapon Icons : {cnt_weap:3d} files -> {out_weap_dir}")
    print(f"[+] Converted UX / HUD Atlases : {cnt_ux:3d} files -> {out_ux_dir}")
    print(f"[+] Converted Squadron Emblems : {cnt_emb:3d} files -> {out_emb_dir}")
    if cnt_font_atlases > 0:
        print(f"[+] Built Master Font Atlases  : {cnt_font_atlases:3d} files ({cnt_font_glyphs} HD intro glyphs) -> {out_fonts_dir}")
    else:
        print("[i] Master Font Atlases        :   0 files (place 'texture converter.zip' in 'mods\' to build MASTER_EN_font_* atlases)")
    if cnt_atl > 0 or cnt_diag > 0 or cnt_hangar_weap > 0:
        print(f"[+] Built Hangar SPW Atlases   : {cnt_atl:3d} files -> {out_atl_dir}")
        print(f"[+] Built Hangar Descriptions  : {cnt_diag:3d} files -> {out_diag_dir}")
        print(f"[+] Built Hangar Weapon Icons  : {cnt_hangar_weap:3d} files -> {out_hangar_weap_dir}")
    else:
        print("[i] Hangar SPW Textures        :   0 files (place 'texture converter.zip' in 'mods\' to build hangar atlases/descriptions/icons)")
    print(f"[+] Companion Multi-Hash JSONs : {cnt_json:3d} files (*.json)")
    print("-" * 78)
    print(f"[SUCCESS] Generated {total_written} HD replacement textures (+ {cnt_json} companion .json files) in:")
    print(f"          {os.path.abspath(output_dir)}")
    print("=" * 78)

    return prompt_post_conversion_cleanup(input_path, tc_zip_path, out_spw_dir, script_path, no_prompt)


if __name__ == "__main__":
    sys.exit(main())