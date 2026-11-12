// ============================================================================
// Ace Combat Zero: The Belkan War (PC Port)
// Built-In Native C++ Texture Pack Auto-Installer & OstrichMods Converter
// File: acz_tex_installer.h (Included directly by acz_hdtex.cpp)
//
// Automatically detects and converts:
//   - "actrilogy-hud-upgrade-v1.2.1.zip" (or extracted SLUS-21346/replacements/hud-upgrade/)
//   - "custom textures.zip" / "texture converter.zip" / "mods.zip" (or "For OstrichMods\")
// directly inside acz_hdtex.dll on startup before ScanTexturesDirectory() runs,
// eliminating the need for any external .bat or Python script.
// ============================================================================
#pragma once

#include <sstream>

namespace acz_installer {

struct TexImgRgba {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> px; // RGBA8 (width * height * 4)

    TexImgRgba() = default;
    TexImgRgba(int w, int h) { allocate(w, h); }

    void allocate(int w, int h) {
        width  = w;
        height = h;
        px.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0u);
    }
    bool empty() const { return width <= 0 || height <= 0 || px.empty(); }
    uint8_t* pixel_ptr(int x, int y) {
        return &px[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u];
    }
    const uint8_t* pixel_ptr(int x, int y) const {
        return &px[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u];
    }
};

// ----------------------------------------------------------------------------
// Lookup Tables (1:1 from ACZ_Textures_installer)
// ----------------------------------------------------------------------------
struct StrPair { const char* k; const char* v; };
struct WeapMapEntry { const char* src_fn; const char* out1; const char* out2; };
struct EmblemSpec { const char* src_fn; const char* out_fn; int sx0, sy0, sx1, sy1; int dx0, dy0, dx1, dy1; };
struct DescClusterSpec { int cid; const char* desc_fn; int sx0, sy0, sx1, sy1; int dx0, dy0, dx1, dy1; };
struct HangarAtlasSlot { const char* weap_fn; int desc_cid; };
struct HangarAtlasSpec { const char* atlas_name; HangarAtlasSlot slots[3]; };
struct HangarTim2WeapSpec { int dx0, dy0, dx1, dy1; const char* hashes[3]; const char* out_name; const char* weap_fn; };
struct HangarTim2DiagSpec { int cid; int dx0, dy0, dx1, dy1; const char* hashes[3]; const char* out_name; };
struct Letterhead64Entry { const char* fn; int glyph_idx; };
struct IntroSub32Entry { const char* fn; int cp1; int cp2; };

static const StrPair kAircraftMap[] = {
    {"1288ce8874c60963-368bd523e0f6c443-00001993.png", "m29a_06006AE1983ADAA8.png"},
    {"138bd4c1f9cc0f92-ecf5b59336262e0a-00001993.png", "x29a_B4DA063F189618C4.png"},
    {"26eba0adbdc237d1-da57d1d1a3645e7b-00001993.png", "f18c_C5DE7270894B083C.png"},
    {"29c8bf1af5eaaca7-720b91ff46125795-00001993.png", "j35j_844E8A9173AD1EF2.png"},
    {"2a9ae8d823db8856-24dc26b36a36b55e-00001993.png", "f35c_E0CFFDE52E113900.png"},
    {"2ac683e94bb8d69f-68010effa2250d89-00001993.png", "f14d_493CE5B4A368E52E.png"},
    {"2ed0628ef5c5f2ca-4bfc82babd4540f7-00001993.png", "f02a_C181058F4EFED7F8.png"},
    {"34bfa5e2c876c316-1004bc381d1a869f-00001993.png", "yf23_2A2D86BDF02106F2.png"},
    {"351647316ecb6d64-5bd84e7f96122b53-00001993.png", "f04e_D39CE5C09778032B.png"},
    {"3d93f340a6af4540-e1ff5002ec08d1e0-00001993.png", "tnd4_2C0EFEA58FFAB12B.png"},
    {"45d969db397e064b-bcafb7dfe1dc9398-00001993.png", "f05e_4042036B7974E846.png"},
    {"4fa26665bcf39a14-e1561577c3e2a825-00001993.png", "f16x_7C25D563486E64F0.png"},
    {"543f338d224438cc-8bfcd06204445049-00001993.png", "su37_2CA861E869083593.png"},
    {"544d335a6f49436-367d0570f6914b9f-00001993.png", "mrgn_4579101E74130E99.png"},
    {"54dc48153c816353-756bc4920a8178b2-00001993.png", "su47_3FFA84155DA91E6C.png"},
    {"5908e6e3de3f3a00-26fd9b7c5889893d-00001993.png", "rm01_D5F2452A114BA0B7.png"},
    {"6800b078a8b5af0-f193c4ad1f3f5d93-00001993.png", "typn_F7452D45ADC556B5.png"},
    {"68796a474ce4e8c1-ecfb78559f378c2b-00001993.png", "m31b_D6D9DFE81F98F859.png"},
    {"6c54f580ef2c4909-d6599b7ebe3fba5d-00001993.png", "f15c_f15e_7BBFF633CB7C5D32.png"},
    {"7ed8165e70cd983f-27fbb65e8d1655b9-00001993.png", "f20a_8DFB806B65E17A75.png"},
    {"8062d239cf15ac74-63cbf9105c886d0d-00001993.png", "a10a_94E4A1201D2DC4F7.png"},
    {"87947be35d0e1c31-a29feaeca6368809-00001993.png", "su27_BCA8414D46EE4590.png"},
    {"8882a0396570e80e-d4be15cda19a7749-00001993.png", "f01a_B7861A3597D47450.png"},
    {"a31c0ac694fba0fb-acfe46803a7fc89-00001993.png", "e06b_D97A62F77D93DE32.png"},
    {"a9afba494bb556eb-2e8b63c688418d6c-00001993.png", "x02a_F483B6C8062A3438.png"},
    {"c2aa95994888d956-aaee28ad980286ee-00001993.png", "su32_5A89454A61AC7B11.png"},
    {"cb24d3d2130e8b92-867f9ec62cdb9ee1-00001993.png", "zoef_D6E7F13AB1C907BF.png"},
    {"d55b92924a8ea6d1-f37e34a68ce0d77-00001993.png", "e18g_13FFFE0E93CAAF0F.png"},
    {"d8988ddf070f7ec0-6cf3573b51a9544-00001993.png", "j39c_EC2787173F88E073.png"},
    {"db8454eb14bf12e-5a7d1f6122f4b304-00001993.png", "f17a_9DD7B050C73AC981.png"},
    {"df6c5267f5f0be48-8bfcd06204445049-00001993.png", "f15s_AFE0689A762F2083.png"},
    {"e865dc677f7db382-c9229a3f3a295ce9-00001993.png", "mr2d_0573278134C82C4A.png"},
    {"f636491f116b2123-fdc9334e10db845a-00001993.png", "m21b_BDF5F8A29A0C2AA9.png"},
    {"f912a981aae5453-9e6c9b646577f5c0-00001993.png", "fa22_1C180245B9B88A9D.png"},
    {"fa27a1dd6aea1ebb-83cb11ec8c0290af-00001993.png", "f16c_7735868F5A08C140.png"},
};

static const WeapMapEntry kWeaponsMap[] = {
    {"17780a775a06417f-5b395e0806a29787-00001553.png", "f01a_spw3_F9BEB845CAFF3998.png", ""},
    {"197ae439eef759e1-b364924c70000301-00001553.png", "m31b_spw3_2D16EB50177EFF62.png", ""},
    {"1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png", "su37_spw3_91DF45EA2ACEE24B.png", ""},
    {"1cf7f6ad376f713e-62852b858c81242b-00001553.png", "mrgn_spw2_8405864A2557042D.png", ""},
    {"1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png", "f02a_spw2_2ACDA5CBF8F320B8.png", ""},
    {"2722168c224dda31-fb2acc81acbc589-00001553.png", "m21b_spw3_059AB79A15989B94.png", ""},
    {"294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png", "fa22_spw3_265ED844345F9BA1.png", ""},
    {"2c5a883bac7f9d52-d769d53355989eb2-00001553.png", "typn_spw2_0CFE653F7AB700D4.png", "rm01_spw3_6C1E590336883DB9.png"},
    {"2e8bde22462e97cd-f869a1d3cec69f20-00001553.png", "m21b_spw2_A59779CD4569253C.png", ""},
    {"3073f6813e500d9a-f09687360676a0f2-00001553.png", "mrgn_spw3_66DA5E58D032BD0F.png", ""},
    {"350eb8028387d459-de67f70996c01abf-00001553.png", "su32_spw2_785945B3568585FA.png", ""},
    {"38131cfd253bd46b-47ed4644be84356f-00001553.png", "j35j_spw1_F7A20D49BE7A4FCF.png", ""},
    {"38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png", "m29a_spw1_1EA3646FD215458F.png", ""},
    {"3e388ad275889e1a-48d7e64a0958b001-00001553.png", "a10a_spw1_9865E33639C60CC1.png", ""},
    {"3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png", "yf23_spw3_D563851A328A5897.png", ""},
    {"411cc74c13352163-fe926e6c6d66aba3-00001553.png", "mr2d_spw2_2D6A61F2E1841277.png", ""},
    {"43ef3bb8935a13d6-a797282f96013320-00001553.png", "f35c_spw2_287E5EAB7C5D9716.png", ""},
    {"4664458b39154bf4-d16bf124bac220ff-00001553.png", "yf23_spw2_9B95EDCCF6E02C75.png", ""},
    {"46e6afb636674472-b527b56ababf8da1-00001553.png", "su27_spw1_8231540C8D788ADD.png", ""},
    {"52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png", "tnd4_spw2_451C7256CC93CB18.png", ""},
    {"52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png", "su27_spw3_1964C4F3EB456382.png", ""},
    {"596cbdf2619f7b8a-962148d78acbe33a-00001553.png", "m29a_spw2_6536B9D7DBF4E705.png", ""},
    {"5ecc5c786debc32c-26e26f6886e0d34-00001553.png", "e06b_spw1_E51501FE1B4644C0.png", ""},
    {"5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png", "a10a_spw3_69EAC6AB9A0588FD.png", ""},
    {"6674fc448194ba9a-482a2ed3d52a2c59-00001553.png", "m31b_spw1_282B6C1A039AB32A.png", ""},
    {"6e5420ff947c1f25-f3c43724228eef6e-00001553.png", "m21b_spw1_155A61C08974143C.png", ""},
    {"76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png", "m29a_spw3_20EA2B44089F5CE4.png", ""},
    {"78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png", "tnd4_spw3_C124F336D322FD47.png", ""},
    {"8448b79b04c936f4-13673bbe6030c235-00001553.png", "j35j_spw3_FC6A8CACE59E6E54.png", ""},
    {"8bcd96efda99fe7a-fed5690acbeed124-00001553.png", "zoef_spw1_FB9FC3E82648A216.png", ""},
    {"92c47ba79a448999-f8e1829d3e674ad7-00001553.png", "m21b_spw0_99A0E0769FB20D4C.png", ""},
    {"9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png", "j39c_spw1_2D475397AB600845.png", ""},
    {"9af02ab15cd6defb-54fe8338087b28e3-00001553.png", "f14d_spw1_6A01A49942644FBA.png", ""},
    {"9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png", "su37_spw2_51FB5F5BA4049DC3.png", ""},
    {"9dcc4303ca58d4ae-3255aab68abec63f-00001553.png", "f01a_spw1_786626BB30FBEECF.png", ""},
    {"a0a76953d9ad8745-79745ac7a081d802-00001553.png", "f15e_spw3_5FC34E95B6BEDA87.png", ""},
    {"a3fe94a35e094a5b-b31023f17d2742cd-00001553.png", "rm01_spw1_3F6AB5615D675E51.png", ""},
    {"a935373d012b23f9-bd68f36a3bb0ae08-00001553.png", "f35c_spw1_0E64EB127C763646.png", ""},
    {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", "f04e_spw1_657335F95C306B91.png", ""},
    {"b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png", "f15s_spw2_3C2985F3CACD20AD.png", ""},
    {"b3cd052625961d0e-ab103eeda9f5b27a-00001553.png", "rm01_spw2_22EAB05B69C3C16F.png", ""},
    {"b95d58c0d63aea15-9ab5acb80064b414-00001553.png", "f14d_spw2_BFB2431B72BD265D.png", ""},
    {"ba57f120b72bdc35-912159579341a858-00001553.png", "x02a_spw3_A944834B961E5D05.png", ""},
    {"bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png", "mr2d_spw3_28CF209F3A16809E.png", ""},
    {"bd9ecdc337510310-74f330c2a5f1c999-00001553.png", "f18c_spw1_DD67FBF0F65ED935.png", ""},
    {"c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png", "f16x_spw3_1E9434CB14021D0E.png", ""},
    {"c343a8decadc17f5-8cc6e211a14a059e-00001553.png", "f04e_spw3_E1867FC688451E97.png", ""},
    {"c55dd3c30e462850-4300ae6334756736-00001553.png", "f02a_spw3_18D4853B7EB2023B.png", ""},
    {"cdfe64556a6bbab-9aded927d43c386e-00001553.png", "mr2d_spw1_66A5E3FDD718D511.png", ""},
    {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", "e06b_spw2_BDA1B5BA948B4559.png", ""},
    {"d6253421a2720ba3-ee47d64d1554ab8c-00001553.png", "f15s_spw3_141AC19E776E787B.png", ""},
    {"d66b021c903d74a0-35db2265694342c2-00001553.png", "mrgn_spw1_4F6CE8FB1DB22F79.png", ""},
    {"d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png", "f04e_spw2_9C3976886EC05F9D.png", ""},
    {"db3b21eea2d6c00c-603e736c5ccb05ce-00001553.png", "a10a_spw0_A42AC6E2DE7D1468.png", ""},
    {"db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png", "a10a_spw2_06B8610C5FC49B95.png", ""},
    {"ddfecded193d0052-6c56475f596994d6-00001553.png", "f01a_spw0_7FA9F929A6E0DC9A.png", ""},
    {"dfe97d3b32645f46-7cea02778833eddf-00001553.png", "su47_spw3_20D368E1179065DF.png", ""},
    {"e129b2f0cd36758f-e896e0119e3ea71b-00001553.png", "mr2d_spw0_D9A680AB5D9773E7.png", ""},
    {"e37af4f2dca452d2-ca27388b97df0ca-00001553.png", "j39c_spw3_5CED8F2FA45170F5.png", ""},
    {"ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png", "e18g_spw2_90034118EAF4A9BE.png", ""},
    {"f222b31462e369bb-4ca89a758f76b82a-00001553.png", "x02a_spw1_ADE75846BB1D29C9.png", ""},
    {"f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png", "typn_spw3_CC6654C55861F733.png", ""},
    {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", "e06b_spw3_93D30AABDE2721FF.png", ""},
};

static const EmblemSpec kEmblemsMap[] = {
    {"1b5dd1b386e329be-93694d338453595e-00001dd3.png", "m10nmark_A87DFCDC3658B5A7.png", 112, 32, 400, 480, 76, 52, 436, 456},
    {"24951acba7b316b7-2baab0f72a36dd1-00001dd3.png", "m01amark_CBBEC4E1F6384A5C.png", 129, 37, 385, 477, 96, 52, 416, 456},
    {"264f26884df4c1f8-49cb05e0e21ff1b3-00001dd3.png", "m16mmark_AD3E175DEFFC645C.png", 110, 32, 402, 480, 76, 52, 436, 456},
    {"3d5453c19cc2a93a-ff54e576f50d601a-00001dd3.png", "m16kmark_14677E40C87757D4.png", 110, 32, 402, 480, 76, 52, 436, 456},
    {"5040703048016a26-d0f929d58cf2ea30-00001dd3.png", "m03nmark_655407D78D147291.png", 112, 33, 399, 479, 76, 52, 436, 456},
    {"72f6b4fb5bacde00-2c81f95a13987bf6-00001dd3.png", "m06amark_CB733A27052F1DB8.png", 113, 32, 400, 479, 76, 52, 436, 456},
    {"90addf4e0f67d523-ff4c8a3d1e4d904c-00001dd3.png", "m03kmark_27B1F74C766F1F88.png", 136, 32, 376, 480, 108, 52, 404, 456},
    {"9775b289551fcd85-be3fc70bd883d7e0-00001dd3.png", "m10kmark_9349DBBDFEB4B9C3.png", 140, 34, 372, 478, 112, 56, 400, 456},
    {"a2b807a07f471713-65b19129de2451d8-00001dd3.png", "m15amark_80B29295644904B5.png", 111, 35, 402, 477, 72, 52, 440, 456},
    {"c788e68c3f731500-9b219a3c671080d2-00001dd3.png", "m10mmark_6125F057A6FFA729.png", 120, 32, 392, 480, 88, 52, 424, 456},
    {"e3ca12d8500c3bfa-5b5f08da9ed05911-00001dd3.png", "m03mmark_1C5B6F181DEED6DF.png", 112, 32, 400, 480, 76, 52, 436, 456},
    {"e974d059dfff0d0b-d802700cc88a0777-00001dd3.png", "m16nmark_92E5F129F09C32AB.png", 140, 27, 371, 487, 112, 48, 400, 460},
};

static const DescClusterSpec kDescClustersMap[] = {
    {0, "3fdf48525eaa6f4f-50d9bb4f3b4c31e3-00001dd3.png", 32, 8, 424, 253, 120, 8, 520, 376},
    {1, "f3ac2ec3e4517d24-f528ad9877ea26c8-00001dd3.png", 32, 18, 425, 246, 120, 56, 528, 328},
    {2, "b624060ac4312c73-3befbaa31c78d496-00001dd3.png", 32, 8, 424, 245, 120, 40, 528, 344},
    {3, "3b9e2c9e3f0f4a50-88405d1baecd3218-00001dd3.png", 35, 5, 417, 256, 120, 40, 512, 344},
    {4, "56ea817ac2db1374-c2800969a031abdd-00001dd3.png", 32, 20, 424, 250, 120, 32, 528, 360},
    {5, "b201ed0c5be98d03-55742ccae9576cb3-00001dd3.png", 31, 16, 425, 247, 96, 56, 544, 328},
    {6, "46189e7714a2db61-924071ca6c3f4072-00001dd3.png", 34, 15, 424, 253, 104, 40, 520, 336},
    {7, "dd5777addc054c1c-b49d07d1f81930d0-00001dd3.png", 31, 29, 425, 231, 120, 72, 520, 312},
    {8, "af900764cef49b5d-4f69e21e4ef073e7-00001dd3.png", 32, 21, 424, 239, 120, 56, 528, 336},
    {9, "5136522b0bf64466-2a75b0a8a7190c07-00001dd3.png", 32, 7, 424, 253, 112, 24, 520, 360},
    {10, "b2415975b7392277-b9334aa2fd29456f-00001dd3.png", 30, 27, 429, 240, 104, 64, 520, 320},
    {11, "dbb0932edca59176-3c8a187d4603fbc1-00001dd3.png", 31, 24, 425, 234, 120, 64, 520, 344},
    {12, "e002c7dceffb07b0-8976b2ae0db32e49-00001dd3.png", 31, 29, 425, 223, 120, 72, 528, 320},
    {13, "201e77548a217d7c-72fd80ce708287f5-00001dd3.png", 37, 12, 424, 248, 104, 40, 520, 336},
    {14, "fe157270c7eb0a86-e1e5e0e89eab2811-00001dd3.png", 32, 33, 424, 234, 112, 64, 520, 328},
    {15, "4e7a43c50cdbc6c6-1321231175ff6b07-00001dd3.png", 31, 23, 425, 243, 112, 56, 528, 328},
    {16, "fe0edc2cb47edc7f-bbb7f2d2715f41b2-00001dd3.png", 32, 36, 425, 222, 120, 64, 520, 320},
    {17, "27bd9581d4cda07f-a9554d40e9f0acec-00001dd3.png", 29, 106, 427, 153, 104, 160, 536, 224},
    {18, "mpbm_desc.png", 95, 56, 417, 246, 120, 72, 520, 312},
};

static const HangarAtlasSpec kHangarAtlasMap[] = {
    {"a10a_F0D04BDF1D284DA1.png", { {"3e388ad275889e1a-48d7e64a0958b001-00001553.png", 0}, {"db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png", 1}, {"5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png", 2} }},
    {"e06b_8EA988653D8C2B9B.png", { {"5ecc5c786debc32c-26e26f6886e0d34-00001553.png", 3}, {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", 4}, {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5} }},
    {"e18g_CDD4B97B06CA1B3B.png", { {"5ecc5c786debc32c-26e26f6886e0d34-00001553.png", 3}, {"ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png", 6}, {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", 4} }},
    {"f01a_1D5BCC417A3B9845.png", { {"9dcc4303ca58d4ae-3255aab68abec63f-00001553.png", 2}, {"3B97DB2F5EB61035.png", 7}, {"17780a775a06417f-5b395e0806a29787-00001553.png", 8} }},
    {"f02a_3DCBB3CF2C9DF9B1.png", { {"17780a775a06417f-5b395e0806a29787-00001553.png", 8}, {"1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png", 6}, {"c55dd3c30e462850-4300ae6334756736-00001553.png", 9} }},
    {"f04e_19A37B37E6057469.png", { {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", 10}, {"d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png", 11}, {"c343a8decadc17f5-8cc6e211a14a059e-00001553.png", 12} }},
    {"f05e_4D452C25FBB55E58.png", { {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5}, {"3B97DB2F5EB61035.png", 7}, {"c343a8decadc17f5-8cc6e211a14a059e-00001553.png", 12} }},
    {"f14d_90003464704FBB86.png", { {"9af02ab15cd6defb-54fe8338087b28e3-00001553.png", 13}, {"b95d58c0d63aea15-9ab5acb80064b414-00001553.png", 14}, {"c343a8decadc17f5-8cc6e211a14a059e-00001553.png", 12} }},
    {"f15c_658A8D9540582856.png", { {"ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png", 6}, {"d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png", 11}, {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5} }},
    {"f15e_9F0018B9C800CD7D.png", { {"c55dd3c30e462850-4300ae6334756736-00001553.png", 9}, {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", 10}, {"a0a76953d9ad8745-79745ac7a081d802-00001553.png", 14} }},
    {"f15s_A698D6196509B9A8.png", { {"db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png", 1}, {"b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png", 13}, {"d6253421a2720ba3-ee47d64d1554ab8c-00001553.png", 15} }},
    {"f16c_61117992C3458826.png", { {"ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png", 6}, {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", 4}, {"d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png", 11} }},
    {"f16x_6350E2CCC30517F7.png", { {"3e388ad275889e1a-48d7e64a0958b001-00001553.png", 0}, {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", 10}, {"c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png", 16} }},
    {"f17a_AC15E0D83C11AE33.png", { {"a0a76953d9ad8745-79745ac7a081d802-00001553.png", 14}, {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", 4}, {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5} }},
    {"f18c_720A744FB814C7EF.png", { {"bd9ecdc337510310-74f330c2a5f1c999-00001553.png", 8}, {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", 10}, {"d6253421a2720ba3-ee47d64d1554ab8c-00001553.png", 15} }},
    {"f20a_EB4A6AFAA377B1E0.png", { {"ae3a10251b90c162-e03e4c7c5afa510a-00001553.png", 10}, {"b95d58c0d63aea15-9ab5acb80064b414-00001553.png", 14}, {"3e388ad275889e1a-48d7e64a0958b001-00001553.png", 0} }},
    {"f35c_E2EE94E7578243C4.png", { {"a935373d012b23f9-bd68f36a3bb0ae08-00001553.png", 8}, {"43ef3bb8935a13d6-a797282f96013320-00001553.png", 15}, {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5} }},
    {"fa22_178DD0DF96594614.png", { {"ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png", 6}, {"b95d58c0d63aea15-9ab5acb80064b414-00001553.png", 14}, {"294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png", 15} }},
    {"j35j_648E8EB6A9BEA04A.png", { {"38131cfd253bd46b-47ed4644be84356f-00001553.png", 10}, {"3B97DB2F5EB61035.png", 7}, {"8448b79b04c936f4-13673bbe6030c235-00001553.png", 2} }},
    {"j39c_CF186F0203B1496A.png", { {"9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png", 2}, {"b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png", 13}, {"e37af4f2dca452d2-ca27388b97df0ca-00001553.png", 15} }},
    {"m21b_AD17BB01F7808901.png", { {"6e5420ff947c1f25-f3c43724228eef6e-00001553.png", 2}, {"2e8bde22462e97cd-f869a1d3cec69f20-00001553.png", 7}, {"2722168c224dda31-fb2acc81acbc589-00001553.png", 5} }},
    {"m29a_9AC6CA2D0F6C3021.png", { {"38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png", 10}, {"596cbdf2619f7b8a-962148d78acbe33a-00001553.png", 2}, {"76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png", 0} }},
    {"m31b_900E239BBBFECCFA.png", { {"6674fc448194ba9a-482a2ed3d52a2c59-00001553.png", 13}, {"2722168c224dda31-fb2acc81acbc589-00001553.png", 5}, {"197ae439eef759e1-b364924c70000301-00001553.png", 11} }},
    {"mr2d_8ECF99F3F694C9EC.png", { {"cdfe64556a6bbab-9aded927d43c386e-00001553.png", 7}, {"411cc74c13352163-fe926e6c6d66aba3-00001553.png", 14}, {"bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png", 0} }},
    {"mrgn_9AC691BF97A302BB.png", { {"d66b021c903d74a0-35db2265694342c2-00001553.png", 17}, {"1cf7f6ad376f713e-62852b858c81242b-00001553.png", 18}, {"3073f6813e500d9a-f09687360676a0f2-00001553.png", 3} }},
    {"rm01_6FCFCCA91CDF4384.png", { {"a3fe94a35e094a5b-b31023f17d2742cd-00001553.png", 8}, {"b3cd052625961d0e-ab103eeda9f5b27a-00001553.png", 6}, {"2c5a883bac7f9d52-d769d53355989eb2-00001553.png", 15} }},
    {"su27_E65621C11C263479.png", { {"46e6afb636674472-b527b56ababf8da1-00001553.png", 6}, {"197ae439eef759e1-b364924c70000301-00001553.png", 11}, {"52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png", 9} }},
    {"su32_603E1220C1EB8029.png", { {"76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png", 0}, {"350eb8028387d459-de67f70996c01abf-00001553.png", 4}, {"46e6afb636674472-b527b56ababf8da1-00001553.png", 6} }},
    {"su37_B340E3E70F82D83B.png", { {"6674fc448194ba9a-482a2ed3d52a2c59-00001553.png", 13}, {"9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png", 1}, {"1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png", 12} }},
    {"su47_84EB97CB90263929.png", { {"38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png", 10}, {"52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png", 9}, {"dfe97d3b32645f46-7cea02778833eddf-00001553.png", 16} }},
    {"tnd4_0A84CA287C1A466A.png", { {"c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png", 16}, {"52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png", 3}, {"78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png", 8} }},
    {"typn_11887AFD1050D2B7.png", { {"b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png", 13}, {"2c5a883bac7f9d52-d769d53355989eb2-00001553.png", 15}, {"f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png", 9} }},
    {"x02a_194FE72DF3296614.png", { {"f222b31462e369bb-4ca89a758f76b82a-00001553.png", 13}, {"2722168c224dda31-fb2acc81acbc589-00001553.png", 5}, {"ba57f120b72bdc35-912159579341a858-00001553.png", 0} }},
    {"x29a_33E8B4E5880EBF11.png", { {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5}, {"db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png", 1}, {"d2379ebef5fc21e2-92e8d3a22733591b-00001553.png", 4} }},
    {"yf23_DB5CE3CDD410E988.png", { {"f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png", 5}, {"4664458b39154bf4-d16bf124bac220ff-00001553.png", 12}, {"3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png", 16} }},
    {"zoef_376441BBEC9C1F58.png", { {"8bcd96efda99fe7a-fed5690acbeed124-00001553.png", 17}, {"b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png", 13}, {"9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png", 1} }},
};

static const HangarTim2WeapSpec kHangarTim2WeapSpecs[] = {
    {112, 16, 208, 304, {"CFA2D375FCC1FFE4", "56A435935CFA3E7C", "147EFE7C51BBF4F6"}, "hangar_weap_f05e_spw1_CFA2D375FCC1FFE4.png", "f7cc59ebaa130b84-4f3727e1b6d7f246-00001553.png"},
    {104, 16, 216, 304, {"EE6126699C475707", "969C41F65599E35F", "C79117E9AF81B3C1"}, "hangar_weap_j35j_spw1_EE6126699C475707.png", "38131cfd253bd46b-47ed4644be84356f-00001553.png"},
    {112, 16, 208, 304, {"5DDDD2026D66F07A", "BC742359A0130922", "C95CEEB1640CA36B"}, "hangar_weap_f01a_spw1_5DDDD2026D66F07A.png", "9dcc4303ca58d4ae-3255aab68abec63f-00001553.png"},
    {120, 16, 200, 304, {"35DBC22B9136D977", "AC80EF2109A98BCF", "3DBA0E66EBE6130D"}, "hangar_weap_m21b_spw1_35DBC22B9136D977.png", "6e5420ff947c1f25-f3c43724228eef6e-00001553.png"},
    {112, 16, 208, 304, {"2E319165C0962F14", "45ADF68B55DD89DC", "5D6C68397F3EB8B7"}, "hangar_weap_f04e_spw1_2E319165C0962F14.png", "ae3a10251b90c162-e03e4c7c5afa510a-00001553.png"},
    {112, 16, 208, 304, {"A0C4D4FBC888E036", "4EA2434B8302738E", "42E59A8A6DE258CD"}, "hangar_weap_m29a_spw1_A0C4D4FBC888E036.png", "38c2268f2ec1fa3c-91cbc9b1dd0ccf30-00001553.png"},
    {120, 16, 200, 304, {"7B315A095AA43355", "EAB957B48EEC155D", "D8F14846602E2C82"}, "hangar_weap_f16c_spw1_7B315A095AA43355.png", "ef2a87c689d5ce21-8ce4bb6627c98537-00001553.png"},
    {120, 16, 200, 304, {"C20413F3356A0AFF", "E09B3ADEE2E2D8A7", "3310FA543A77C1D1"}, "hangar_weap_f18c_spw1_C20413F3356A0AFF.png", "bd9ecdc337510310-74f330c2a5f1c999-00001553.png"},
    {112, 16, 208, 304, {"98B1F48B6820C303", "62F591C4BE2001EB", "3C6CE62679ED7EB9"}, "hangar_weap_a10a_spw1_98B1F48B6820C303.png", "3e388ad275889e1a-48d7e64a0958b001-00001553.png"},
    {104, 16, 216, 304, {"088C8AF8F77197B3", "96B07118015C2C6B", "BA393723E6B57056"}, "hangar_weap_m31b_spw1_088C8AF8F77197B3.png", "6674fc448194ba9a-482a2ed3d52a2c59-00001553.png"},
    {120, 16, 200, 304, {"85BED3A9CB88801C", "F2C13336B34777B4", "9DFDFAB66A20A9F4"}, "hangar_weap_mr2d_spw1_85BED3A9CB88801C.png", "cdfe64556a6bbab-9aded927d43c386e-00001553.png"},
    {120, 16, 200, 304, {"BE57249B51C426FC", "E0B99FFB4428FCF4", "93FA363E5E5677E3"}, "hangar_weap_e06b_spw1_BE57249B51C426FC.png", "5ecc5c786debc32c-26e26f6886e0d34-00001553.png"},
    {112, 16, 208, 304, {"2CDCB303780BA5F8", "0820F2E113386860", "521769DC4B26061B"}, "hangar_weap_su27_spw1_2CDCB303780BA5F8.png", "46e6afb636674472-b527b56ababf8da1-00001553.png"},
    {120, 16, 216, 304, {"8CD5C5514D756229", "4DCE095EF19EA3D1", "10149C4CB54F975F"}, "hangar_weap_f14d_spw1_8CD5C5514D756229.png", "9af02ab15cd6defb-54fe8338087b28e3-00001553.png"},
    {120, 16, 200, 304, {"D9E5E34B8DCB86D2", "21EFE923760D3C5A", "F6174021CCCDFB01"}, "hangar_weap_j39c_spw1_D9E5E34B8DCB86D2.png", "9aece1e1b8a29e5a-7a05b36c71239d05-00001553.png"},
    {120, 16, 200, 304, {"69A39F228699DE1F", "45FC7AE18CC4EF77", "96642B4034E7A9B5"}, "hangar_weap_tnd4_spw1_69A39F228699DE1F.png", "c0608d105e4e5c79-3f45d36ee96a8f34-00001553.png"},
    {104, 16, 216, 304, {"2BFF9C7ED749DC96", "AA2552C9F60D35DE", "44F34DEC80ECCF3C"}, "hangar_weap_f02a_spw1_2BFF9C7ED749DC96.png", "17780a775a06417f-5b395e0806a29787-00001553.png"},
    {128, 16, 200, 304, {"1FA936B0074347A7", "A4ADB515D279D7BF", "32D25826501254F4"}, "hangar_weap_f15e_spw1_1FA936B0074347A7.png", "c55dd3c30e462850-4300ae6334756736-00001553.png"},
    {120, 16, 200, 304, {"D2FE457BE9B50F03", "0D0AE0B78A610F9B", "678779E977B35DB7"}, "hangar_weap_f17a_spw1_D2FE457BE9B50F03.png", "a0a76953d9ad8745-79745ac7a081d802-00001553.png"},
    {128, 16, 192, 304, {"137398EF6F8F329F", "DC705C7B6C797577", "EF76DEE56720C9B2"}, "hangar_weap_f35c_spw1_137398EF6F8F329F.png", "a935373d012b23f9-bd68f36a3bb0ae08-00001553.png"},
    {112, 16, 208, 304, {"9445E413F263712E", "FBE70A8275B20A86", "901398266F701965"}, "hangar_weap_rm01_spw1_9445E413F263712E.png", "a3fe94a35e094a5b-b31023f17d2742cd-00001553.png"},
    {120, 16, 200, 304, {"4F80CE9F9E847061", "C29623495D7E5A29", "DBB65A4F93B1F234"}, "hangar_weap_typn_spw1_4F80CE9F9E847061.png", "b0c9a3ec7fdda1f0-d408e9cca91c0d0f-00001553.png"},
    {112, 16, 208, 304, {"E8501C293E0566DD", "AF6864E4537F2BC5", "3C92CF3EE2561BE2"}, "hangar_weap_su32_spw1_E8501C293E0566DD.png", "76d6c85970b4e321-e72fe1d5b8838e3c-00001553.png"},
    {112, 16, 208, 304, {"DFFDB7362B0AAAE5", "8A2B27DABC7F65BD", "77D0B3ED141A4163"}, "hangar_weap_f15s_spw1_DFFDB7362B0AAAE5.png", "db881b7a0d7961b4-7de37204e1ff3fb7-00001553.png"},
    {128, 16, 192, 304, {"2001CB5B29CF0380", "9458FCEF3C0D4F58", "2C1718B70F7A5E76"}, "hangar_weap_x02a_spw1_2001CB5B29CF0380.png", "f222b31462e369bb-4ca89a758f76b82a-00001553.png"},
    {96, 16, 224, 304, {"8A84AB341FD81E0B", "A512C686F83E99F3", "C714DAE82029D74C"}, "hangar_weap_mrgn_spw1_8A84AB341FD81E0B.png", "d66b021c903d74a0-35db2265694342c2-00001553.png"},
    {96, 16, 224, 304, {"7904A659A587F072", "3BCC612F2D0BF41A", "2422EE4B45B26CEB"}, "hangar_weap_zoef_spw1_7904A659A587F072.png", "8bcd96efda99fe7a-fed5690acbeed124-00001553.png"},
    {120, 16, 200, 304, {"19897FD506CDFDCB", "B047DA2E2EDC82B3", "9A470186A34E4020"}, "hangar_weap_f05e_spw2_19897FD506CDFDCB.png", "3B97DB2F5EB61035.png"},
    {120, 16, 200, 304, {"2151C0A66C452010", "48396377334C9AC8", "2CECD1533AB50DBC"}, "hangar_weap_m21b_spw2_2151C0A66C452010.png", "2e8bde22462e97cd-f869a1d3cec69f20-00001553.png"},
    {128, 16, 192, 304, {"944C68908A79DDBB", "F431FBA2515D2D43", "60957694A3C78467"}, "hangar_weap_f04e_spw2_944C68908A79DDBB.png", "d8bc7c133101bd93-dbbbfb190bbbcb7d-00001553.png"},
    {120, 16, 200, 304, {"E4CAF508147F3B2A", "9B0D8E9C77F75C72", "9B8285C925790337"}, "hangar_weap_m29a_spw2_E4CAF508147F3B2A.png", "596cbdf2619f7b8a-962148d78acbe33a-00001553.png"},
    {120, 16, 200, 304, {"401510E242FD8149", "B7B0FAFA5FC91341", "E9EC017C0F990042"}, "hangar_weap_f20a_spw2_401510E242FD8149.png", "b95d58c0d63aea15-9ab5acb80064b414-00001553.png"},
    {104, 16, 216, 304, {"6542C1C2796216A9", "ACB2121F489073E1", "6221B027AF486F45"}, "hangar_weap_f16c_spw2_6542C1C2796216A9.png", "d2379ebef5fc21e2-92e8d3a22733591b-00001553.png"},
    {120, 16, 200, 304, {"57DA7480F4C5C174", "C60B5926AF55594C", "E0B24080B18414EB"}, "hangar_weap_m31b_spw2_57DA7480F4C5C174.png", "2722168c224dda31-fb2acc81acbc589-00001553.png"},
    {120, 16, 200, 304, {"3D49A5B6B0B0E41D", "EE4F41A29467A525", "0BEB5A81533D93D2"}, "hangar_weap_mr2d_spw2_3D49A5B6B0B0E41D.png", "411cc74c13352163-fe926e6c6d66aba3-00001553.png"},
    {120, 16, 200, 304, {"732D3C53673CB93B", "B07621AC83E99583", "8AF23142736508A2"}, "hangar_weap_su27_spw2_732D3C53673CB93B.png", "197ae439eef759e1-b364924c70000301-00001553.png"},
    {64, 16, 248, 304, {"BD76A1C00744D03A", "EE5789A0B51E2042", "B12158A3D18C3E6D"}, "hangar_weap_tnd4_spw2_BD76A1C00744D03A.png", "52184c7e9c1c67d9-850e66d446c0bcb6-00001553.png"},
    {120, 16, 200, 304, {"49D93438073C416D", "9BBABABB5E8DF535", "C43E400CBF22CBCD"}, "hangar_weap_f02a_spw2_49D93438073C416D.png", "1f3ab3d7d599f805-19d115d8ae53ad8f-00001553.png"},
    {136, 16, 184, 304, {"B0FB9A6F2154ED62", "88D308F3047C43EA", "1D5F5C10799D4C54"}, "hangar_weap_f35c_spw2_B0FB9A6F2154ED62.png", "43ef3bb8935a13d6-a797282f96013320-00001553.png"},
    {120, 16, 200, 304, {"66DD7F6DF4DE1847", "ADB0814BCE0A71DF", "273345FC5D3D2D04"}, "hangar_weap_rm01_spw2_66DD7F6DF4DE1847.png", "b3cd052625961d0e-ab103eeda9f5b27a-00001553.png"},
    {104, 16, 216, 304, {"6E1F6A8FEC33D8EB", "422CD1B46259BB83", "B2CBC3265C762CF2"}, "hangar_weap_typn_spw2_6E1F6A8FEC33D8EB.png", "2c5a883bac7f9d52-d769d53355989eb2-00001553.png"},
    {112, 16, 208, 304, {"F9F02B902A923C3F", "956A11DE849395A7", "942318A6932F2E0F"}, "hangar_weap_su32_spw2_F9F02B902A923C3F.png", "350eb8028387d459-de67f70996c01abf-00001553.png"},
    {128, 16, 192, 304, {"E1F1837ABD30B33E", "ECC0538A20F24E86", "1B2DF96BED2A43C6"}, "hangar_weap_yf23_spw2_E1F1837ABD30B33E.png", "4664458b39154bf4-d16bf124bac220ff-00001553.png"},
    {120, 16, 200, 304, {"E797023306AE8D13", "D748E3F072A6E78B", "E437E0824462B823"}, "hangar_weap_su47_spw2_E797023306AE8D13.png", "52ced0bee7ccc7e9-1ab514154b153d6f-00001553.png"},
    {128, 16, 192, 304, {"F6D1B38CE245CA6A", "AB2DAC67FA2CDF22", "A9C693E92E829941"}, "hangar_weap_su37_spw2_F6D1B38CE245CA6A.png", "9b1655fb4c9215e6-c8794470c46cbe6e-00001553.png"},
    {112, 16, 208, 304, {"649644B0FFDB35DB", "EA700DCC7B06BA83", "721A6A3A5B590688"}, "hangar_weap_mrgn_spw2_649644B0FFDB35DB.png", "1cf7f6ad376f713e-62852b858c81242b-00001553.png"},
    {120, 16, 200, 304, {"30844AB64E1F2249", "904D8489F9B208E1", "F205ABA8380A7AB0"}, "hangar_weap_f05e_spw3_30844AB64E1F2249.png", "c343a8decadc17f5-8cc6e211a14a059e-00001553.png"},
    {104, 16, 216, 304, {"25FAE3655BFFA8F3", "5FDB3AAF55A35F3B", "68187FDBE16C4BAD"}, "hangar_weap_j35j_spw3_25FAE3655BFFA8F3.png", "8448b79b04c936f4-13673bbe6030c235-00001553.png"},
    {120, 16, 200, 304, {"B1A58297257ADB7A", "7C130833048EB7D2", "39C43B56DFE3005A"}, "hangar_weap_f18c_spw3_B1A58297257ADB7A.png", "d6253421a2720ba3-ee47d64d1554ab8c-00001553.png"},
    {128, 16, 192, 304, {"4E3D13DE1C6F5733", "75D92B4F93526B6B", "1FEBC754AB857C5D"}, "hangar_weap_a10a_spw3_4E3D13DE1C6F5733.png", "5fc72f2cf3e1a6bc-113a38e73fdc9c3c-00001553.png"},
    {104, 16, 216, 304, {"DD6B70BA361EE20B", "0AB8534738E85B33", "44CC8D01D8FA7044"}, "hangar_weap_mr2d_spw3_DD6B70BA361EE20B.png", "bbf6a831b24a7578-c963f5aeb4219d6f-00001553.png"},
    {104, 16, 216, 304, {"C0443621B3B2DCF2", "97487A889B30B19A", "DD39B7DDB5874A01"}, "hangar_weap_j39c_spw3_C0443621B3B2DCF2.png", "e37af4f2dca452d2-ca27388b97df0ca-00001553.png"},
    {112, 16, 208, 304, {"1812447569099D16", "A4ED02770ECC7BCE", "B089100270189D30"}, "hangar_weap_tnd4_spw3_1812447569099D16.png", "78774c45ae68bf5a-a2ea2e352d8513bb-00001553.png"},
    {128, 16, 192, 304, {"90BBA7B3886A4A14", "99AA4D1573E7863C", "1001C1AFBD49C72B"}, "hangar_weap_typn_spw3_90BBA7B3886A4A14.png", "f6a74dfbf7e72993-5c45b0a5b39279ee-00001553.png"},
    {120, 16, 200, 304, {"1ED7D11252A2812E", "9293110B39C6F846", "0AB8A82F8DFEA912"}, "hangar_weap_yf23_spw3_1ED7D11252A2812E.png", "3f33c5aec1bb824d-23bf29e36e8af3a7-00001553.png"},
    {112, 16, 200, 304, {"961BAC79F6935C20", "20EC5CEE7EA32788", "AF659F99289B8E4D"}, "hangar_weap_su47_spw3_961BAC79F6935C20.png", "dfe97d3b32645f46-7cea02778833eddf-00001553.png"},
    {112, 16, 208, 304, {"1D9CF7D49090C2E2", "245FE59D4755B63A", "E0204A84031A9AF1"}, "hangar_weap_su37_spw3_1D9CF7D49090C2E2.png", "1a3b9ca600a9a7d0-3984cdba7e4333f3-00001553.png"},
    {112, 16, 208, 304, {"D5BFAA3A3EBCFEFA", "C05C5FC2D7BB8E42", "3065051E0F54F819"}, "hangar_weap_fa22_spw3_D5BFAA3A3EBCFEFA.png", "294eb3a2c7da9628-1f8625c05eafcfc4-00001553.png"},
    {128, 16, 192, 304, {"36C85406DE6504F4", "B2FEF6B7DA7F126C", "DF5D5B50938D6A75"}, "hangar_weap_x02a_spw3_36C85406DE6504F4.png", "ba57f120b72bdc35-912159579341a858-00001553.png"},
    {96, 16, 224, 304, {"8A1127F62E6AC9BE", "2BE57315A7502046", "F534D6B4894E144F"}, "hangar_weap_mrgn_spw3_8A1127F62E6AC9BE.png", "3073f6813e500d9a-f09687360676a0f2-00001553.png"},
};

static const HangarTim2DiagSpec kHangarTim2DiagSpecs[] = {
    {7, 56, 136, 456, 376, {"5ABB139679DF8C3A", "53B1C19300561FA2", "6616704A6FCB1613"}, "hangar_diag_07_e92_5ABB139679DF8C3A.png"},
    {11, 56, 128, 464, 408, {"80761C7BA8C74B26", "D2B742365D1FA1AE", "1018FB8F0073326E"}, "hangar_diag_11_e93_80761C7BA8C74B26.png"},
    {14, 48, 128, 456, 392, {"51AE0AACDD342738", "1FA0379AC5A3F4A0", "975812E423A05468"}, "hangar_diag_14_e94_51AE0AACDD342738.png"},
    {9, 48, 88, 464, 424, {"372C93B73D452D22", "51F18E48C3B17F3A", "5F3183E9BBDBD7BF"}, "hangar_diag_09_e95_372C93B73D452D22.png"},
    {1, 56, 120, 464, 392, {"1E767AECF138302E", "3FD506797F254496", "18D234B1CA1AAF4C"}, "hangar_diag_01_e96_1E767AECF138302E.png"},
    {16, 56, 128, 464, 384, {"CC2674D8F5F15135", "CE126CABCAAC2D5D", "52ED2972602A30B1"}, "hangar_diag_16_e97_CC2674D8F5F15135.png"},
    {12, 56, 120, 464, 384, {"D62CE3B95A1BEF94", "85EC8D287CB8162C", "2F3885164A84A8E6"}, "hangar_diag_12_e98_D62CE3B95A1BEF94.png"},
    {2, 56, 104, 464, 408, {"67D7BA13ECAB40B6", "BF973B29A8FEEEAE", "46E9AADE6F881EC8"}, "hangar_diag_02_e99_67D7BA13ECAB40B6.png"},
    {15, 48, 120, 464, 392, {"6B9A1945EA61DC86", "1147F6DADD03497E", "4F642A78161B2F91"}, "hangar_diag_15_e100_6B9A1945EA61DC86.png"},
    {8, 56, 120, 464, 400, {"85B0B814F4A32F6C", "D4E88433A8C8D0D4", "DD8FF2B25D22F48B"}, "hangar_diag_08_e101_85B0B814F4A32F6C.png"},
    {4, 56, 96, 464, 424, {"1A045EDF5F5FB97B", "FAE83DACDB9E5803", "CD003199C0B4C79C"}, "hangar_diag_04_e102_1A045EDF5F5FB97B.png"},
    {0, 56, 72, 464, 440, {"07BA923F19F49884", "7CB729C14396C59C", "42298569F1870559"}, "hangar_diag_00_e103_07BA923F19F49884.png"},
    {6, 40, 104, 456, 400, {"CB36C9D7E460ABFF", "DA0C8B3FA814DDF7", "86B2A25E4F175437"}, "hangar_diag_06_e104_CB36C9D7E460ABFF.png"},
    {13, 32, 104, 456, 400, {"847C878E5961CC21", "20E4A5A83A79BC59", "F9D9DF74D54980F1"}, "hangar_diag_13_e105_847C878E5961CC21.png"},
    {10, 40, 128, 464, 384, {"2F1B879F052F7070", "764167A720266D78", "FB8713221D35254C"}, "hangar_diag_10_e106_2F1B879F052F7070.png"},
    {5, 32, 120, 480, 392, {"2E9EE736675B9EC3", "1714B347D2377B9B", "ACDE72AF68D20161"}, "hangar_diag_05_e107_2E9EE736675B9EC3.png"},
    {3, 56, 104, 448, 408, {"C33CE224CAE5CB78", "0B0D6CA90DA5AE10", "C115D46D7C01D715"}, "hangar_diag_03_e108_C33CE224CAE5CB78.png"},
    {17, 32, 224, 472, 288, {"AB94EC6D96B38BA4", "B82928DC8A76E0FC", "DA3A3E83AF5606EC"}, "hangar_diag_17_e109_AB94EC6D96B38BA4.png"},
    {18, 56, 136, 456, 376, {"5296FDD584CF77FA", "C68CAE8D1639DB62", "DAF486E54A19AF25"}, "hangar_diag_18_e110_5296FDD584CF77FA.png"},
};

static const StrPair kUxSpecs[] = {
    {"711475816fc05634-c5bfd55d3a21f4eb-00002214.png", "001.png"},
    {"d00645d18395b834-c3c4161ae78bdabb-00002214.png", "000.png"},
};

static const Letterhead64Entry kLetterhead64Map[] = {
    {"58fadb6ec2a61d9-6252899fa067fd04-00001994.png", 0},
    {"b58a9ba90009c45c-6252899fa067fd04-00001994.png", 1},
    {"1889fd02183494ff-6252899fa067fd04-00001994.png", 2},
    {"6e6722250f07402e-6252899fa067fd04-00001994.png", 3},
    {"7f7605f4d5b613f9-6252899fa067fd04-00001994.png", 4},
    {"d79667bc9e8453b0-6252899fa067fd04-00001994.png", 5},
    {"638c0f748648c894-6252899fa067fd04-00001994.png", 6},
    {"2102e1c3e0e2f987-6252899fa067fd04-00001994.png", 7},
    {"961a2f25068c1899-6252899fa067fd04-00001994.png", 8},
    {"ae1bd754bf602e6-6252899fa067fd04-00001994.png", 9},
    {"2d5590226f638d2f-6252899fa067fd04-00001994.png", 10},
    {"f7a448e1576cad89-6252899fa067fd04-00001994.png", 11},
    {"d8b29df2733365e8-6252899fa067fd04-00001994.png", 12},
    {"a9057a6157fa0332-6252899fa067fd04-00001994.png", 13},
    {"e25265184d31b953-6252899fa067fd04-00001994.png", 14},
    {"c9d2fff2cbfb924c-6252899fa067fd04-00001994.png", 15},
    {"1a7e1982c1e344ac-6252899fa067fd04-00001994.png", 16},
    {"e382d2a788f7e01c-6252899fa067fd04-00001994.png", 17},
    {"dca2f51328b6cbb5-6252899fa067fd04-00001994.png", 18},
    {"68ac3905293fbb04-6252899fa067fd04-00001994.png", 19},
    {"a9fdff9a15ba5ce1-6252899fa067fd04-00001994.png", 20},
    {"dbfbb22e66f8a13-6252899fa067fd04-00001994.png", 21},
    {"75cc3ebeec7cfe46-6252899fa067fd04-00001994.png", 22},
};

static const IntroSub32Entry kIntroSub32UpperMap[] = {
    {"493074ad4cf797ae-9beeeb5765c760d6-00001554.png", 0x0030, 0x004F},
    {"bf85e980913d4c1f-9beeeb5765c760d6-00001554.png", 0x0031, -1},
    {"c5d47c911bfb6b07-9beeeb5765c760d6-00001554.png", 0x0032, -1},
    {"14a47cee5c41ba5b-9beeeb5765c760d6-00001554.png", 0x0033, -1},
    {"cdacb610ac12eb12-9beeeb5765c760d6-00001554.png", 0x0035, -1},
    {"a752220b1d3a7b87-9beeeb5765c760d6-00001554.png", 0x0036, -1},
    {"bd31563cbae5cbed-9beeeb5765c760d6-00001554.png", 0x0037, -1},
    {"a68d85b6e6ecedca-9beeeb5765c760d6-00001554.png", 0x0038, -1},
    {"a71eb05759eae02-9beeeb5765c760d6-00001554.png", 0x0039, -1},
    {"323ac31e13747ea5-9beeeb5765c760d6-00001554.png", 0x0041, -1},
    {"fc0aaf344db6a7ce-9beeeb5765c760d6-00001554.png", 0x0042, -1},
    {"a20b20b26d305893-9beeeb5765c760d6-00001554.png", 0x0044, -1},
    {"b4cdad0dd55b687c-9beeeb5765c760d6-00001554.png", 0x0046, -1},
    {"acb25de2a7b81149-9beeeb5765c760d6-00001554.png", 0x004C, -1},
    {"c0a2948904069623-9beeeb5765c760d6-00001554.png", 0x004E, -1},
    {"511137132ebab7ea-9beeeb5765c760d6-00001554.png", 0x0051, -1},
    {"f99e77e5e1e9e66c-9beeeb5765c760d6-00001554.png", 0x0053, -1},
    {"a15e9cf74bd4387f-9beeeb5765c760d6-00001554.png", 0x0054, -1},
    {"b6d3b7054751bad2-9beeeb5765c760d6-00001554.png", 0x0055, -1},
};

static const IntroSub32Entry kIntroSub32LowerMap[] = {
    {"89387acd9be7996d-9beeeb5765c760d6-00001554.png", 0x0061, -1},
    {"2eff1c5b86bed65a-9beeeb5765c760d6-00001554.png", 0x0063, -1},
    {"1ec20f52cfbd021a-9beeeb5765c760d6-00001554.png", 0x0064, -1},
    {"9c94881fe89f767b-9beeeb5765c760d6-00001554.png", 0x0065, -1},
    {"4783dd6356dec740-9beeeb5765c760d6-00001554.png", 0x0066, -1},
    {"e253fc86a099592-9beeeb5765c760d6-00001554.png", 0x0067, -1},
    {"6e4ecec51b969db4-9beeeb5765c760d6-00001554.png", 0x0068, -1},
    {"71ae3d13e0d6ddc0-9beeeb5765c760d6-00001554.png", 0x0069, -1},
    {"4733cacad6594289-9beeeb5765c760d6-00001554.png", 0x006B, -1},
    {"230b90de25623b38-9beeeb5765c760d6-00001554.png", 0x006C, -1},
    {"baffbd778f7307b5-9beeeb5765c760d6-00001554.png", 0x006E, -1},
    {"f7cb37ae071703cf-9beeeb5765c760d6-00001554.png", 0x006F, -1},
    {"7049e97d81353cd9-9beeeb5765c760d6-00001554.png", 0x0070, -1},
    {"e056357db951de7a-9beeeb5765c760d6-00001554.png", 0x0072, -1},
    {"eb96df5a4ee6f8aa-9beeeb5765c760d6-00001554.png", 0x0073, -1},
    {"55a6c2c702a155e0-9beeeb5765c760d6-00001554.png", 0x0074, -1},
    {"4d8f5224e405ee90-9beeeb5765c760d6-00001554.png", 0x0076, -1},
};

static const StrPair kRuntime16x16AltExactHashes[] = {
    {"009181FA85625A75", "BEDF31C28B4E7F57"},
    {"01833BFD855E0355", "04086394507E027B"},
    {"02AC41DF06099FAF", "A71589FB2E4155D5"},
    {"05DC48A46D504D3C", "98E10ECC1353834E"},
    {"09DD28A8A679177A", "F2D251E560A658F8"},
    {"0B6CEBB5AB1FDC64", "A1B6F4836C319A52"},
    {"0BD511BD789206B7", "C4C1D1849AF93F99"},
    {"13AD0397F62804DD", "A2E0449256A28177"},
    {"21627C6A34B7B181", "4C4E75FEE079481F"},
    {"2C8A0684288DA8BD", "65EF29627868DBD3"},
    {"2CE9DE2416310C86", "CF684C1EAA4FA4D4"},
    {"2D8E2F344DD64B90", "325EB12591E1DD22"},
    {"3D3735C44B002E89", "BC0F3A542EDA471B"},
    {"3E3D7458617C7F6D", "FBFBAE591997B8DB"},
    {"50C65313684426EB", "20B42018A71EB23D"},
    {"55AAB4ACC30DAC58", "3D067D483FE15C9A"},
    {"567D031FBFA29BDB", "A18F5FE3E7BDE609"},
    {"593D0F1A7EA04508", "84AE13341BB4EEFE"},
    {"61C89F584D2AC531", "0AD9F056805E96FB"},
    {"6B588A1251D02315", "D37D031AB9E67553"},
    {"6BF648049A74126E", "4225ADDAF0B92480"},
    {"74B4DF42C7BDFDAD", "762BD59C1DAE4D37"},
    {"854094C4AB739F17", "41A76FF297EF7999"},
    {"9028DF7F6297DE1E", "C443ADB250B7AC7C"},
    {"913717DD40FABE58", "BF19E0AFF9AC69DE"},
    {"993F3AB02D21247C", "6F406933E77C0316"},
    {"9A8000E00DC6791F", "4FFC13DD879C6A15"},
    {"A54BD270D719F185", "F3B3AD4A4B0AD42F"},
    {"AC11470E0395A1BD", "C387F2C1B2C17B53"},
    {"B12A5674F04705DD", "AC7C35788848582F"},
    {"BD2BA6B0C875D732", "E8C85C6DE4E51D0C"},
    {"C7FF5BAF88E20773", "CFD692815AE7491D"},
    {"C977AA3A9017C535", "A79EE531F1EEB2CB"},
    {"D67E78ECDB18A984", "80A6D7A9B5C1A8EE"},
    {"DF341EA364C07870", "7D550476DF37D862"},
    {"E032BFB293FBCD84", "5EB315787DEE94DE"},
    {"E8117A7498EDE8F2", "BA3DABD88E278BC0"},
    {"EACE52702EB252F1", "F32AA7466B2DE0E7"},
    {"ED845B6144F74B68", "DC5638B78F151D52"},
    {"F0B0AB6A5861C873", "D10A0580AA06B435"},
    {"F139440CDDC2323E", "E63A558F224944F4"},
    {"F95379253669CFCE", "0421CE17BEC3E114"},
    {"FD45878F6D38C407", "F47ED412F5AC65E1"},
};

static const char kFallbackUgbs1xB64[] = "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAABi0lEQVR4nL3XPUscQRjA8Z9yRYpUKSwsLEI4EkWQgHbmBY58Ar9ByhRWFiLCgaVICpEUKYSkSiVCihAIhJCAvbl4iJUgWIulHBYzwmLWZW+9maeZnef1z8wyz8xYt9t1D+nFcaZpgvF7FN/HIObYzw3QwWMs4x3aWMoJ8B6/CvPP2MgJ0MJeYf5T2I4sAJP4d6vgg6ibzAHQwWmJ/hzPcwCs4qxE38NmDoABvpboD3CVGuBNHMv2+lHM10kJsFjD52VKgDb6FfZ+9EkGADsNbSMDGKkMCzBdw+dpSoCLGj6XKQEusVBhX0gNcIH5Cvt8aoC+6j1u40dKgD+q224LJykBvsWYqRLbhNALvqcEgGOslOjXhYY0lDQB2LsjboCtHACfhKW+HTuuuk+MDIDwszW6A44KgND/byDaTXM1BXiG14X42ajLBgCvCt8vmiZpCnCE34X5XxzmAujFgl8Kuo/CCTg0xLAAh8KT7EOJbVs4iHoltjulhYfqX6fXsIs5/8MP8ARvhZdSrfrXpSZEY8AoqEUAAAAASUVORK5CYII=";
static const char kFallbackMpbmDesc1xB64[] = "iVBORw0KGgoAAAANSUhEUgAAAFAAAABACAYAAACNx/A2AAAD20lEQVR4nO2aP0xTURSHP4yjhoGlLAQ2MSYEFh3cKAuLbDqUhcRQJoyTTrhAjJNMwuICAyYmwmgKM8EBB/8w0ri0mxJxxuGcx3t9bQF77is8c76kae+9597e9+u9551zX3sWFhZwOufaZU8g77iARlxAIy6gERfQiAtoxAU04gIayVrAIrAJHAIn+r6p9f8FWQlYQgSrACPAR2Ba30e0/kTtcs31DMbcB0aBVaCcaltPfF4B1oCnwFgG8+gKoVfgISLebZrFS1NWu1Htl0tCCrgJDCJb9eCCfX6q/aD2zx2hBBwGHiBirCErqggUWtgW9FUEdtX+ifYfDjSfrhFKwF1gC/FxR8iKqgAzwN2UbV3rK2p3BLzT/ruB5tM1Qgg4DPQS+7w3ibYPwF6LPm8Tn18hopZ1nFytwhACzgNVRASAHX2vAt9p3sYFoKbtEItZ17r5AHNqRwEJnVq5lo4IIeAjYCNR/g30A0NaTm/LqDwE9KTaNnS8YBeoFJHwqgb0Ef/YZqwCFpBtt5Oo2yOeYD/i5/a1vK/l/oR98mJ2dLw7xnlFc5sHfiH+dlTrlwOMfYpVwOhCt9u014EJ4lhvVMvtVkA0TnSn7oQCEhLVgNeIT+5HIoQjtSnp60aH33FKNw4TtoGXyMqDOI1r9wIJbWrn2LXrewwMJL7/m9b1IXFnNP4acM96cVmkcmmKwDPkBjGIpHg/zrBfREKaT//4PX+Q1VdHUsMCcboIsvp69XPa93aMVcCv+l6k9TYuICtuC5hCttYssqVabePolOY9jXlzJ9T1OyNfOKf1pQBjn2LdwnXklx1v0ZYMV6a0bkrLNcT/pI+1xnW8YBeoc3wO3EJcyf2AYwfxgRvEvy5I5nFCY7iSJCp/QVZn8mYxR2NIFJJIyBdcsThwGfEt0R0tWo2DNIYrSaLwBiStgzgkChpmtKDOFYoDQU5eqsTb7nGibabJurl+kdjhV7n4Sc6VIFQYM4mcppSQlVVFzvqW2tgvATeJ07mH2n8y0Hy6Rqgw5gAJT9aQQLldYJ3kGPGHw0jOvErOVh+EDaTLyIqqcPGHRkVEvCrnn2BfSUJnIkPAZ0TElTPsIp9XUfv0nTo3ZJHKjSF55yyNjzJLxI84a9o+TY4fKEF2ufA6ki5NII8yBxD/OKDlCW0PGTBfClnnwttc7IaSW/yvHUZcQCPWLXxyvkku6Ph4yyrgtLF/7rEKmPu7qBX3gUZcQCMuoBEX0IgLaMQFNOICGnEBjbiARlxAIy6gERfQiAtoxAU04gIacQGNuIBGXEAjLqARF9CIC2jEBTTiAhpxAY38BQS2xNbd1AorAAAAAElFTkSuQmCC";
static const char kSynthIntro4_256B64[] = "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAYAAABccqhmAAAKK0lEQVR42u3dbW/W5hkG4LMJIaBASCMIpQgxWoQHCE3VNGnaL96n/Ylp0raq24eudK462q5qoRClEIgyGpLtg2/vcdO8x89LmuOQrKYJeWzFuk9ftu+XBAAAAAAAAAAAAAAAAAAAmFBvDXsHdV3PJTmf5GySmSRT5Uftf7fKlm3f381aVVVPnTqYwACo6/pMkutJ3klyJcmlJG0ItAHQNvLpJBudAJg6QAC8SPIoycdVVX3rFMKEBEBd1+8muZ3kZpJfJFlKcjnJfKcKmN5WBbxJstkJhP0CYCXJx0k+TPLHqqqeOI1wNGd6bPzvJ7mf5E4JgVtJbpQAWChX/z5sJlksVcNKEgEARzTVU+N/tzT+B0l+neQ3SX6b5G65DZjp8Zink3yQ5F6SW3VdX3IaYYwBUMr926VRPihhMD/E455J8l6Sa6XCAMYRAHVdL6V56HerU/7PjuDYL5dbgTmnEcZXAVxO8m653785osafDN4szDqNMN4AuFrK8SsjPPbpnm9jQAAcsvw/U8rwy2le+Y3S6zR9CN44jTCeCmChbItlG6W1JOslCIAxBMClTuOfH/Gxr5YA+MFphPFVAJfKNmprZVMBwJgCYHFM5b9bABhnANR1fS7Nw7+2q++ovU7zANBDQBhDBXAtg8E+4wiArTTjAjadRhhPAFzNaN/972TaaYQRBkAZgHO9VABvj+nYz2j8MJ4K4Eaa7r9LYyr/2yv/megJCKMLgLquL5YAuJbxvP/vHvuUAIDRVgA3S/k/rod/x61egKM0orquL5fG3wbARX9COD0VwI2yLZXy/7w/IZyCACjTft3Ydv9vLD6ckgqgbfzt67+LAgBOtgPNCtyZ9qu9+rdTfbcBspmmS27bPXcqg/f03a+BkxYAGTz4axf8aOfim0ozIOdVfjo6b7Zs52P6LjiZAVCW9nqn0/ivlgCYLlf+9TTz83+f5Hn5/zYA5tK8Ktws+xIAcMIqgO6cf+0AoG45v14a/3dJlksVkHLFXyhfn834OgwBxwiAxdLo2237Ih/raWbnWU7yOMnL8v25cjtwNs0Dww1/bjiZFUA76edOI/9el0a/nORpJwDaPgLz5feN24eTFACl3/9iuQXYaQWe6dKwV8tzgOXyHCBJLpSr/2KpElQAcMIqgMVOBbDT1X+zVABrpeF3A+CHchvQTt2lAoATGADttlu33/XSyF+UKmClfP9NmslC10oYqADghAVAu/7ebpN+vOk8A3i+LQBSvt9O3Lnlzw0nJAA69/+L2X3Yb1v+r5YAeF5V1Yvy+1Pl++0tgPn7YcJM7VP+t3P+z+8RAOvlSv+iqqrvOz97Ub7/KoNlvEzgCScoANoZf/a6/3+ZwVuA/6uqaqv8rPsQ0INAmPQAKIt+Xu5UAdkjAF5l8ABwu+4twLCeA5gZCHpuPEudANhr1Z+1cpV/XlXVT67u5XnAWpL/dG4D+jz26RhlCL0HwLVOACzsEwDPd7n6d//NsFbxMSko9BkAZeafdtDPYvae969t/Cv73CYM4yHgtACAHgOgvPprZ/y5ksGw390a/3KS5fbV3z4BoB8ATGoAlMbfTvnd3gLsdfX/LsmTsu2lLf8FAEyYM3Vdv53mVd9iBrP+LKV5+j+3x+9+neTLsu0XAG3jn+nx2Dfj1SIcLwCS3C8Nfb40/DYAFrL7+/9/J/lXks+2df7ZyZvOvs70eOxvynOFdacRjh4A90pDP5/BqL+rJQB2msJrI8lnST5N8s8D7GOjc/Xvcx2BtvELADhGADwojfNsafQLnVuAnUr2z0sAPKyq6skB9rFV9nM2/b6zf5PkdVVVa04jHC8A2tdp7Qy+uy36+V2SL8qV/5MD7mNqCFf/tgIwwAiOGQC/7Px/Wwlc2OHfvkrzxP/LJF9UVfXtAffRTg1+oedj34o3C3DsALh6wH/7IoNXf98cYh9zaV4n9r2QqA5AMKJGtFEqgHbij+eH2EfbnbjvAGirFWDIAbCVwew/h+3VdzX7jyk4itkk83VdX3IaYbgBMNXZ2hV/9lXX9bkMlhOf6fnYL6Z5bXnNaYThBsBMfrzU11KZM2A/99N0L14awrEvpFmq7NYBjwU4YgCk2/iTvJfkXpn3b7er/50kv0pyO03vwr5dLtXF3SQfOJVweIe5craN/2YGve8W6rr+Ks1w4LbDz1xpmPdLw7yTg79pOGwF8F6aEYkbdV3Pp+mk9LiqKv0DoOcAmC1X8q3y9UIJg6dppv5Kmp5+5ztVwp2yDct7aR5OXijHdi/Jk7quHyX5p16CsLe36rr+7xF+71m58q5kMPd/e0sxm8GCotcymiXBv0nTR2ElTT+FT5J8lOTPVVW9dJrh+BVA15X8eKmwzU4FMA7XO88ZnmXw1mE1yV+cZthZX73pJmlyzitJfleeP9wtE50AQwyASXOhPA+4Ff0E4NQFQNI8ILyenZc1B37mATCT/ec1BAHwM9bOdAScwgAATnEAmDMQTmkAbKTprKQjEJzCAHiUpofgstMMpysAXiV5mGYC08dOM+zs5ziO/lmSv5ftU2MBYO8A+NMeP+92722X4toqX291tu6qv7Np3r9fTTNhxyi6CO82GOihUwx7B8AfOg18e8PffouwsUsIdM2l6YF3M81Q4LsZ3rv49TSLlHzWued/Ur42HBgOEAC/3+PqP9UJga1tAZBOCHTNJ3k/zYIj62l65D0Y0vE/SvK3Uu4/jAlB4HABcIgFPg7qRZKv67pey6A77o30Pyvw8xIA/0gz7v9DpxMOZ5hvAT5Ks4DoVxnOq7jlNEuUf1r2BUxKAFRVtZXmNdzTHG4hkcNUAE/SLFNmiTCYsAogad7BD6s33svy2d7zwyQGQFVVL9JMyzWM/vjrSVbLPoAJrADahvp6CJ/7Ogb6wMQHwJuybfT4mRudzwUmOAC6/Qj6sjXC4wcBcAwzQ9rPVPpfcBQEQF/quj6b5FySs+l3gZDZ8pnnyj6ACawA5st2YQiffaHz+cAEBsDFznaSPhsEQE+NdD79jwNI+cx5AQCTGwDnR1ABmPYbJjQAZtI8sJsbwmfPlc/2JgAmNADa+QSG0Uhn8uP5CoAJC4B28pBhjNZrP1dvQJjQAFhPspZmQFDfVstnGw8AExoAq2km6hzWhCArQwoXEAA9NdLHaabu6nsw0KMM5hsAJi0Aylj9z9NM2PlRTyGwkcGU35+bDwCObhQLgzxMsli+ftb5+qhWymf+Neb9h2N5axQ7qet6Kcm9JLdz/L77q21VUVXVU6cQJjwAOkFwKcfvFLSm7AcAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAkfgfrMk1+67MhhYAAAAASUVORK5CYII=";


// ----------------------------------------------------------------------------
// String & Path Helpers
// ----------------------------------------------------------------------------
static inline std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static inline std::string ToUpperAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

static inline std::string TrimAscii(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

static bool ReadWholeFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
    out.clear();
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;
    FILE* f = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&f, p.wstring().c_str(), L"rb") != 0 || !f) return false;
    _fseeki64(f, 0, SEEK_END);
    int64_t sz = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
#else
    f = fopen(p.string().c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    int64_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif
    if (sz < 0 || sz > 512LL * 1024LL * 1024LL) {
        fclose(f);
        return false;
    }
    if (sz == 0) {
        fclose(f);
        return true;
    }
    out.resize(static_cast<size_t>(sz));
    bool ok = (fread(out.data(), 1, out.size(), f) == out.size());
    fclose(f);
    return ok;
}

static bool WriteWholeFileBytes(const fs::path& p, const void* data, size_t size) {
    std::error_code ec;
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }
    FILE* f = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&f, p.wstring().c_str(), L"wb") != 0 || !f) return false;
#else
    f = fopen(p.string().c_str(), "wb");
    if (!f) return false;
#endif
    bool ok = (size == 0) || (fwrite(data, 1, size, f) == size);
    fclose(f);
    return ok;
}

static bool WriteWholeFileText(const fs::path& p, const std::string& text) {
    return WriteWholeFileBytes(p, text.data(), text.size());
}

static void WriteCompanionHashJson(const fs::path& png_path, const std::string& comment, int w, int h, const std::vector<std::string>& hashes) {
    fs::path json_path = png_path;
    json_path.replace_extension(".json");
    std::string s = "{\n";
    s += "  \"comment\": \"" + comment + "\",\n";
    if (w > 0 && h > 0) {
        s += "  \"width\": " + std::to_string(w) + ",\n";
        s += "  \"height\": " + std::to_string(h) + ",\n";
    }
    s += "  \"hashes\": [\n";
    for (size_t i = 0; i < hashes.size(); ++i) {
        s += "    \"" + hashes[i] + "\"" + (i + 1 < hashes.size() ? ",\n" : "\n");
    }
    s += "  ]\n}\n";
    WriteWholeFileText(json_path, s);
}

// ----------------------------------------------------------------------------
// Base64 Decoder
// ----------------------------------------------------------------------------
static std::vector<uint8_t> DecodeBase64(const char* b64) {
    std::vector<uint8_t> out;
    if (!b64) return out;
    static const int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    uint32_t val = 0;
    int bits = -8;
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(b64); *p; ++p) {
        int8_t d = kDec[*p];
        if (d == -1) continue;
        if (d == -2) break;
        val = (val << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return out;
}

// ----------------------------------------------------------------------------
// Self-Contained RFC 1951 DEFLATE Decompressor + ZIP Reader
// ----------------------------------------------------------------------------
struct HuffTable {
    uint16_t counts[16]{};
    uint16_t symbols[288]{};

    bool build(const uint8_t* lengths, int num_syms) {
        std::memset(counts, 0, sizeof(counts));
        for (int i = 0; i < num_syms; ++i) {
            if (lengths[i] > 15) return false;
            counts[lengths[i]]++;
        }
        counts[0] = 0;
        uint16_t offs[16]{};
        uint16_t acc = 0;
        for (int i = 1; i <= 15; ++i) {
            offs[i] = acc;
            acc = static_cast<uint16_t>(acc + counts[i]);
        }
        for (int i = 0; i < num_syms; ++i) {
            uint8_t len = lengths[i];
            if (len != 0) {
                symbols[offs[len]++] = static_cast<uint16_t>(i);
            }
        }
        return true;
    }
};

struct BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;

    bool ensure(int n) {
        while (bitcnt < n) {
            if (pos >= size) return false;
            bitbuf |= static_cast<uint32_t>(data[pos++]) << bitcnt;
            bitcnt += 8;
        }
        return true;
    }
    uint32_t bits(int n) {
        if (n == 0) return 0;
        ensure(n);
        uint32_t v = bitbuf & ((1u << n) - 1u);
        bitbuf >>= n;
        bitcnt -= n;
        return v;
    }
    void align_byte() {
        int drop = bitcnt & 7;
        bitbuf >>= drop;
        bitcnt -= drop;
    }
    int decode(const HuffTable& ht) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            if (!ensure(1)) return -1;
            code |= static_cast<int>(bitbuf & 1u);
            bitbuf >>= 1;
            bitcnt -= 1;
            int cnt = ht.counts[len];
            if (code - cnt < first) {
                return ht.symbols[index + (code - first)];
            }
            index += cnt;
            first += cnt;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

static bool InflateRaw(const uint8_t* src, size_t src_len, std::vector<uint8_t>& out, size_t expected_uncomp_size = 0) {
    out.clear();
    if (expected_uncomp_size > 0) out.reserve(expected_uncomp_size);
    BitReader br{src, src_len, 0, 0, 0};

    static const uint16_t kLenBase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t kLenExtra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t kDistBase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const uint8_t kDistExtra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const uint8_t kClOrder[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    bool bfinal = false;
    while (!bfinal) {
        if (!br.ensure(3)) return false;
        bfinal = (br.bits(1) != 0);
        uint32_t btype = br.bits(2);
        if (btype == 0) {
            br.align_byte();
            if (!br.ensure(32)) return false;
            uint16_t len  = static_cast<uint16_t>(br.bits(16));
            uint16_t nlen = static_cast<uint16_t>(br.bits(16));
            if (len != static_cast<uint16_t>(~nlen)) return false;
            while (len > 0) {
                if (!br.ensure(8)) return false;
                out.push_back(static_cast<uint8_t>(br.bits(8)));
                --len;
            }
        } else if (btype == 1 || btype == 2) {
            HuffTable litlen_ht, dist_ht;
            if (btype == 1) {
                uint8_t ll_lens[288];
                for (int i = 0; i <= 143; ++i) ll_lens[i] = 8;
                for (int i = 144; i <= 255; ++i) ll_lens[i] = 9;
                for (int i = 256; i <= 279; ++i) ll_lens[i] = 7;
                for (int i = 280; i <= 287; ++i) ll_lens[i] = 8;
                uint8_t d_lens[30];
                for (int i = 0; i < 30; ++i) d_lens[i] = 5;
                litlen_ht.build(ll_lens, 288);
                dist_ht.build(d_lens, 30);
            } else {
                if (!br.ensure(14)) return false;
                int hlit  = static_cast<int>(br.bits(5)) + 257;
                int hdist = static_cast<int>(br.bits(5)) + 1;
                int hclen = static_cast<int>(br.bits(4)) + 4;
                uint8_t cl_lens[19]{};
                for (int i = 0; i < hclen; ++i) {
                    if (!br.ensure(3)) return false;
                    cl_lens[kClOrder[i]] = static_cast<uint8_t>(br.bits(3));
                }
                HuffTable cl_ht;
                if (!cl_ht.build(cl_lens, 19)) return false;
                uint8_t all_lens[320]{};
                int idx = 0;
                int total = hlit + hdist;
                while (idx < total) {
                    int sym = br.decode(cl_ht);
                    if (sym < 0) return false;
                    if (sym < 16) {
                        all_lens[idx++] = static_cast<uint8_t>(sym);
                    } else if (sym == 16) {
                        if (idx == 0 || !br.ensure(2)) return false;
                        int rep = static_cast<int>(br.bits(2)) + 3;
                        uint8_t prev = all_lens[idx - 1];
                        while (rep-- > 0 && idx < total) all_lens[idx++] = prev;
                    } else if (sym == 17) {
                        if (!br.ensure(3)) return false;
                        int rep = static_cast<int>(br.bits(3)) + 3;
                        while (rep-- > 0 && idx < total) all_lens[idx++] = 0;
                    } else if (sym == 18) {
                        if (!br.ensure(7)) return false;
                        int rep = static_cast<int>(br.bits(7)) + 11;
                        while (rep-- > 0 && idx < total) all_lens[idx++] = 0;
                    } else {
                        return false;
                    }
                }
                if (!litlen_ht.build(all_lens, hlit)) return false;
                if (!dist_ht.build(all_lens + hlit, hdist)) return false;
            }

            while (true) {
                int sym = br.decode(litlen_ht);
                if (sym < 0) return false;
                if (sym < 256) {
                    out.push_back(static_cast<uint8_t>(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    int lidx = sym - 257;
                    if (lidx < 0 || lidx >= 29) return false;
                    int match_len = kLenBase[lidx] + static_cast<int>(br.bits(kLenExtra[lidx]));
                    int dsym = br.decode(dist_ht);
                    if (dsym < 0 || dsym >= 30) return false;
                    size_t dist = static_cast<size_t>(kDistBase[dsym] + br.bits(kDistExtra[dsym]));
                    if (dist == 0 || dist > out.size()) return false;
                    for (int k = 0; k < match_len; ++k) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        } else {
            return false;
        }
    }
    return true;
}

struct ZipEntryMeta {
    std::string filename;
    uint16_t method = 0;
    uint32_t comp_size = 0;
    uint32_t uncomp_size = 0;
    uint32_t local_hdr_off = 0;
    bool is_dir = false;
};

struct ZipArchiveReader {
    std::vector<uint8_t> raw;
    std::vector<ZipEntryMeta> entries;

    bool open(const fs::path& zip_path) {
        entries.clear();
        raw.clear();
        if (!ReadWholeFileBytes(zip_path, raw) || raw.size() < 22) return false;

        size_t max_back = std::min<size_t>(raw.size(), 65557u);
        int64_t eocd_pos = -1;
        for (int64_t p = static_cast<int64_t>(raw.size()) - 22; p >= static_cast<int64_t>(raw.size() - max_back); --p) {
            if (*reinterpret_cast<const uint32_t*>(raw.data() + p) == 0x06054B50u) {
                eocd_pos = p;
                break;
            }
        }
        if (eocd_pos < 0) return false;

        uint16_t cd_count = *reinterpret_cast<const uint16_t*>(raw.data() + eocd_pos + 10);
        uint32_t cd_size  = *reinterpret_cast<const uint32_t*>(raw.data() + eocd_pos + 12);
        uint32_t cd_off   = *reinterpret_cast<const uint32_t*>(raw.data() + eocd_pos + 16);
        if (static_cast<uint64_t>(cd_off) + cd_size > raw.size()) return false;

        size_t p = cd_off;
        for (uint16_t i = 0; i < cd_count && p + 46 <= raw.size(); ++i) {
            if (*reinterpret_cast<const uint32_t*>(raw.data() + p) != 0x02014B50u) break;
            uint16_t method    = *reinterpret_cast<const uint16_t*>(raw.data() + p + 10);
            uint32_t comp_sz   = *reinterpret_cast<const uint32_t*>(raw.data() + p + 20);
            uint32_t uncomp_sz = *reinterpret_cast<const uint32_t*>(raw.data() + p + 24);
            uint16_t fn_len    = *reinterpret_cast<const uint16_t*>(raw.data() + p + 28);
            uint16_t ext_len   = *reinterpret_cast<const uint16_t*>(raw.data() + p + 30);
            uint16_t cmt_len   = *reinterpret_cast<const uint16_t*>(raw.data() + p + 32);
            uint32_t loc_off   = *reinterpret_cast<const uint32_t*>(raw.data() + p + 42);
            if (p + 46 + fn_len > raw.size()) break;

            std::string fname(reinterpret_cast<const char*>(raw.data() + p + 46), fn_len);
            for (char& c : fname) {
                if (c == '\\') c = '/';
            }
            bool is_dir = (!fname.empty() && fname.back() == '/');
            entries.push_back({fname, method, comp_sz, uncomp_sz, loc_off, is_dir});
            p += 46u + fn_len + ext_len + cmt_len;
        }
        return !entries.empty();
    }

    bool extract_entry(const ZipEntryMeta& e, std::vector<uint8_t>& out_data) const {
        out_data.clear();
        if (e.is_dir) return false;
        size_t lo = e.local_hdr_off;
        if (lo + 30 > raw.size()) return false;
        if (*reinterpret_cast<const uint32_t*>(raw.data() + lo) != 0x04034B50u) return false;
        uint16_t fn_len  = *reinterpret_cast<const uint16_t*>(raw.data() + lo + 26);
        uint16_t ext_len = *reinterpret_cast<const uint16_t*>(raw.data() + lo + 28);
        size_t data_off = lo + 30u + fn_len + ext_len;
        if (data_off + e.comp_size > raw.size()) return false;

        if (e.method == 0) {
            out_data.assign(raw.begin() + data_off, raw.begin() + data_off + e.comp_size);
            return true;
        } else if (e.method == 8) {
            return InflateRaw(raw.data() + data_off, e.comp_size, out_data, e.uncomp_size);
        }
        return false;
    }
};

// ----------------------------------------------------------------------------
// WIC Memory PNG Decoder & File PNG Encoder
// ----------------------------------------------------------------------------
#ifdef _WIN32
static bool DecodePngBytesViaWic(const uint8_t* png_bytes, size_t png_size, TexImgRgba& out) {
    out.width = 0;
    out.height = 0;
    out.px.clear();
    if (!png_bytes || png_size < 8) return false;

    const HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr_co;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream) {
        factory->Release();
        return false;
    }
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(png_bytes), static_cast<DWORD>(png_size));
    if (FAILED(hr)) {
        stream->Release();
        factory->Release();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        stream->Release();
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        stream->Release();
        factory->Release();
        return false;
    }

    UINT w = 0, h = 0;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 16384 || h > 16384) {
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return false;
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return false;
    }

    const size_t stride = static_cast<size_t>(w) * 4u;
    const size_t total  = stride * static_cast<size_t>(h);
    out.width  = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.px.resize(total);

    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(total), out.px.data());
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();

    if (FAILED(hr)) {
        out.width = 0;
        out.height = 0;
        out.px.clear();
        return false;
    }
    return true;
}

static bool SavePngFileViaWic(const fs::path& out_path, const TexImgRgba& img) {
    if (img.empty()) return false;
    std::error_code ec;
    if (out_path.has_parent_path()) {
        fs::create_directories(out_path.parent_path(), ec);
    }

    const HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr_co;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory) {
        IWICStream* stream = nullptr;
        if (SUCCEEDED(factory->CreateStream(&stream)) && stream) {
            std::wstring wpath = out_path.wstring();
            if (SUCCEEDED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))) {
                IWICBitmapEncoder* encoder = nullptr;
                if (SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && encoder) {
                    if (SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) {
                        IWICBitmapFrameEncode* frame = nullptr;
                        if (SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && frame) {
                            if (SUCCEEDED(frame->Initialize(nullptr)) &&
                                SUCCEEDED(frame->SetSize(static_cast<UINT>(img.width), static_cast<UINT>(img.height)))) {
                                WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                                if (SUCCEEDED(frame->SetPixelFormat(&fmt))) {
                                    const size_t px_cnt = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
                                    std::vector<uint8_t> bgra(px_cnt * 4u);
                                    for (size_t i = 0; i < px_cnt; ++i) {
                                        bgra[i * 4u + 0] = img.px[i * 4u + 2];
                                        bgra[i * 4u + 1] = img.px[i * 4u + 1];
                                        bgra[i * 4u + 2] = img.px[i * 4u + 0];
                                        bgra[i * 4u + 3] = img.px[i * 4u + 3];
                                    }
                                    const UINT stride = static_cast<UINT>(img.width * 4);
                                    const UINT total  = static_cast<UINT>(bgra.size());
                                    if (SUCCEEDED(frame->WritePixels(static_cast<UINT>(img.height), stride, total, bgra.data())) &&
                                        SUCCEEDED(frame->Commit()) &&
                                        SUCCEEDED(encoder->Commit())) {
                                        frame->Release();
                                        encoder->Release();
                                        stream->Release();
                                        factory->Release();
                                        return true;
                                    }
                                }
                            }
                            frame->Release();
                        }
                    }
                    encoder->Release();
                }
            }
            stream->Release();
        }
        factory->Release();
    }

    // Fallback to uncompressed PNG encoder if WIC PNG encoder is unavailable
    std::vector<uint8_t> png_bytes;
    EncodeRgbaToPngBytes(img.px.data(), static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), png_bytes);
    return WriteWholeFileBytes(out_path, png_bytes.data(), png_bytes.size());
}
#endif

// ----------------------------------------------------------------------------
// Image Resampling (Separable Lanczos-3 & Nearest) and Manipulation Primitives
// ----------------------------------------------------------------------------
static inline float Lanczos3Kernel(float x) {
    x = std::fabs(x);
    if (x < 1e-6f) return 1.0f;
    if (x >= 3.0f) return 0.0f;
    constexpr float kPi = 3.14159265358979323846f;
    float pix = kPi * x;
    float pix3 = pix * (1.0f / 3.0f);
    return (std::sin(pix) / pix) * (std::sin(pix3) / pix3);
}

static void ResizeLanczosFloatChannel(const float* src, int sw, int sh, float* dst, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (sw == dw && sh == dh) {
        std::memcpy(dst, src, static_cast<size_t>(sw) * static_cast<size_t>(sh) * sizeof(float));
        return;
    }

    struct TapSpan { int i0; int count; std::vector<float> w; };
    auto build_taps = [](int in_sz, int out_sz) -> std::vector<TapSpan> {
        std::vector<TapSpan> spans(out_sz);
        float ratio = static_cast<float>(in_sz) / static_cast<float>(out_sz);
        float scale = std::max(1.0f, ratio);
        float support = 3.0f * scale;
        for (int o = 0; o < out_sz; ++o) {
            float center = (static_cast<float>(o) + 0.5f) * ratio - 0.5f;
            int left  = static_cast<int>(std::ceil(center - support));
            int right = static_cast<int>(std::floor(center + support));
            if (left < 0) left = 0;
            if (right >= in_sz) right = in_sz - 1;
            if (left > right) {
                left = right = std::clamp(static_cast<int>(std::round(center)), 0, in_sz - 1);
            }
            int cnt = right - left + 1;
            spans[o].i0 = left;
            spans[o].count = cnt;
            spans[o].w.resize(cnt);
            float sum = 0.0f;
            float inv_scale = 1.0f / scale;
            for (int k = 0; k < cnt; ++k) {
                float wt = Lanczos3Kernel((center - static_cast<float>(left + k)) * inv_scale);
                spans[o].w[k] = wt;
                sum += wt;
            }
            if (std::fabs(sum) > 1e-8f) {
                float inv_sum = 1.0f / sum;
                for (int k = 0; k < cnt; ++k) spans[o].w[k] *= inv_sum;
            } else {
                spans[o].w[0] = 1.0f;
            }
        }
        return spans;
    };

    std::vector<TapSpan> x_taps = build_taps(sw, dw);
    std::vector<TapSpan> y_taps = build_taps(sh, dh);
    std::vector<float> tmp(static_cast<size_t>(sh) * static_cast<size_t>(dw), 0.0f);

    for (int y = 0; y < sh; ++y) {
        const float* srow = src + static_cast<size_t>(y) * sw;
        float* trow = tmp.data() + static_cast<size_t>(y) * dw;
        for (int x = 0; x < dw; ++x) {
            const TapSpan& ts = x_taps[x];
            float acc = 0.0f;
            const float* sp = srow + ts.i0;
            for (int k = 0; k < ts.count; ++k) acc += sp[k] * ts.w[k];
            trow[x] = acc;
        }
    }

    for (int y = 0; y < dh; ++y) {
        const TapSpan& ts = y_taps[y];
        float* drow = dst + static_cast<size_t>(y) * dw;
        for (int x = 0; x < dw; ++x) {
            float acc = 0.0f;
            for (int k = 0; k < ts.count; ++k) {
                acc += tmp[static_cast<size_t>(ts.i0 + k) * dw + x] * ts.w[k];
            }
            drow[x] = acc;
        }
    }
}

static void ResizeLanczosGrayFloat(const uint8_t* src_u8, int sw, int sh, std::vector<float>& dst_f, int dw, int dh) {
    const size_t n_in = static_cast<size_t>(sw) * static_cast<size_t>(sh);
    std::vector<float> src_f(n_in);
    for (size_t i = 0; i < n_in; ++i) {
        src_f[i] = static_cast<float>(src_u8[i]);
    }
    dst_f.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh), 0.0f);
    ResizeLanczosFloatChannel(src_f.data(), sw, sh, dst_f.data(), dw, dh);
    for (float& v : dst_f) {
        v = std::clamp(std::round(v), 0.0f, 255.0f);
    }
}

static TexImgRgba ResizePremultipliedLanczos(const TexImgRgba& src, int target_w, int target_h) {
    TexImgRgba out;
    if (src.empty() || target_w <= 0 || target_h <= 0) return out;
    if (src.width == target_w && src.height == target_h) return src;

    const size_t n_in  = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
    const size_t n_out = static_cast<size_t>(target_w) * static_cast<size_t>(target_h);
    std::vector<float> ch_in[4], ch_out[4];
    for (int c = 0; c < 4; ++c) {
        ch_in[c].resize(n_in);
        ch_out[c].resize(n_out);
    }
    for (size_t i = 0; i < n_in; ++i) {
        float a = static_cast<float>(src.px[i * 4u + 3]) * (1.0f / 255.0f);
        ch_in[0][i] = std::clamp(std::round(static_cast<float>(src.px[i * 4u + 0]) * a), 0.0f, 255.0f);
        ch_in[1][i] = std::clamp(std::round(static_cast<float>(src.px[i * 4u + 1]) * a), 0.0f, 255.0f);
        ch_in[2][i] = std::clamp(std::round(static_cast<float>(src.px[i * 4u + 2]) * a), 0.0f, 255.0f);
        ch_in[3][i] = static_cast<float>(src.px[i * 4u + 3]);
    }
    for (int c = 0; c < 4; ++c) {
        ResizeLanczosFloatChannel(ch_in[c].data(), src.width, src.height, ch_out[c].data(), target_w, target_h);
    }

    out.allocate(target_w, target_h);
    for (size_t i = 0; i < n_out; ++i) {
        float a_res  = std::clamp(std::round(ch_out[3][i]), 0.0f, 255.0f);
        float a_norm = a_res * (1.0f / 255.0f);
        if (a_norm > (1.0f / 255.0f)) {
            float inv_a = 1.0f / std::max(a_norm, 1e-5f);
            for (int c = 0; c < 3; ++c) {
                float v = std::clamp(std::round(ch_out[c][i]), 0.0f, 255.0f) * inv_a;
                out.px[i * 4u + c] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
            }
        }
        out.px[i * 4u + 3] = static_cast<uint8_t>(a_res);
    }
    return out;
}

static TexImgRgba ResizeNearest(const TexImgRgba& src, int target_w, int target_h) {
    TexImgRgba out;
    if (src.empty() || target_w <= 0 || target_h <= 0) return out;
    if (src.width == target_w && src.height == target_h) return src;
    out.allocate(target_w, target_h);
    for (int y = 0; y < target_h; ++y) {
        int sy = std::min(src.height - 1, (y * src.height) / target_h);
        for (int x = 0; x < target_w; ++x) {
            int sx = std::min(src.width - 1, (x * src.width) / target_w);
            const uint8_t* sp = src.px.data() + (static_cast<size_t>(sy) * src.width + sx) * 4u;
            uint8_t* dp = out.px.data() + (static_cast<size_t>(y) * target_w + x) * 4u;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    return out;
}

static TexImgRgba CropImage(const TexImgRgba& src, int x0, int y0, int x1, int y1) {
    TexImgRgba out;
    x0 = std::clamp(x0, 0, src.width);
    x1 = std::clamp(x1, x0, src.width);
    y0 = std::clamp(y0, 0, src.height);
    y1 = std::clamp(y1, y0, src.height);
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) return out;
    out.allocate(w, h);
    for (int y = 0; y < h; ++y) {
        const uint8_t* srow = src.px.data() + (static_cast<size_t>(y0 + y) * src.width + x0) * 4u;
        uint8_t* drow = out.px.data() + static_cast<size_t>(y) * w * 4u;
        std::memcpy(drow, srow, static_cast<size_t>(w) * 4u);
    }
    return out;
}

static void PasteImage(TexImgRgba& dst, const TexImgRgba& src, int dx0, int dy0) {
    if (dst.empty() || src.empty()) return;
    for (int y = 0; y < src.height; ++y) {
        int dy = dy0 + y;
        if (dy < 0 || dy >= dst.height) continue;
        for (int x = 0; x < src.width; ++x) {
            int dx = dx0 + x;
            if (dx < 0 || dx >= dst.width) continue;
            const uint8_t* sp = src.px.data() + (static_cast<size_t>(y) * src.width + x) * 4u;
            uint8_t* dp = dst.px.data() + (static_cast<size_t>(dy) * dst.width + dx) * 4u;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
}

static void Ps2AlphaToPc(TexImgRgba& img) {
    if (img.empty()) return;
    uint8_t max_a = 0;
    const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < n; ++i) {
        if (img.px[i * 4u + 3] > max_a) max_a = img.px[i * 4u + 3];
    }
    if (max_a <= 180u) {
        for (size_t i = 0; i < n; ++i) {
            uint32_t a = img.px[i * 4u + 3];
            img.px[i * 4u + 3] = static_cast<uint8_t>(std::min<uint32_t>(255u, (a * 255u + 64u) / 128u));
        }
    }
}

static float ComputePercentile(std::vector<float>& vals, float pct) {
    if (vals.empty()) return 0.0f;
    std::sort(vals.begin(), vals.end());
    if (vals.size() == 1) return vals[0];
    float pos = (pct * 0.01f) * static_cast<float>(vals.size() - 1);
    size_t idx = static_cast<size_t>(pos);
    float frac = pos - static_cast<float>(idx);
    if (idx + 1 < vals.size()) {
        return vals[idx] + frac * (vals[idx + 1] - vals[idx]);
    }
    return vals.back();
}

// ============================================================================
// 5. Image Conversion Processors (1:1 Port of OstrichMods Converter)
// ============================================================================

static TexImgRgba ProcessAircraftIcon(const TexImgRgba& src) {
    TexImgRgba img = src;
    if (img.width == 384 && img.height == 384) {
        img = CropImage(img, 0, 0, 288, 288);
    } else if (img.width == img.height && img.width > 0 && (img.width % 64) == 0) {
        int crop_dim = (img.width * 48) / 64;
        img = CropImage(img, 0, 0, crop_dim, crop_dim);
    }
    Ps2AlphaToPc(img);
    return img;
}

static TexImgRgba ProcessWeaponIcon(const TexImgRgba& src) {
    TexImgRgba img = src;
    Ps2AlphaToPc(img);
    const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = img.px[i * 4u + 3];
        if (a > 0) {
            img.px[i * 4u + 0] = 128;
            img.px[i * 4u + 1] = 128;
            img.px[i * 4u + 2] = 128;
        } else {
            img.px[i * 4u + 0] = 0;
            img.px[i * 4u + 1] = 0;
            img.px[i * 4u + 2] = 0;
        }
    }
    return img;
}

static TexImgRgba ProcessCustomUgbsIcon(const TexImgRgba& src) {
    if (src.empty()) return {};
    const int w = src.width;
    const int h = src.height;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);

    std::vector<float> a_vec(n, 0.0f);
    std::vector<float> nz;
    nz.reserve(n / 2);
    float max_a = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        uint8_t raw_a = src.px[i * 4u + 3];
        if (raw_a <= 18) raw_a = 0;
        float af = static_cast<float>(raw_a);
        a_vec[i] = af;
        if (af > 0.0f) {
            nz.push_back(af);
            if (af > max_a) max_a = af;
        }
    }
    if (!nz.empty() && max_a <= 180.0f) {
        float p95 = std::max(125.0f, ComputePercentile(nz, 95.0f));
        for (size_t i = 0; i < n; ++i) {
            a_vec[i] = std::min(255.0f, (a_vec[i] * 255.0f) / p95);
        }
    }

    std::vector<uint8_t> a_u8(n, 0);
    for (size_t i = 0; i < n; ++i) {
        a_u8[i] = static_cast<uint8_t>(std::clamp(std::round(a_vec[i]), 0.0f, 255.0f));
    }

    TexImgRgba out(256, 256);
    if (w != 256 || h != 256) {
        float scale = std::min(256.0f / static_cast<float>(w), 256.0f / static_cast<float>(h));
        int nw = std::max(1, static_cast<int>(std::round(static_cast<float>(w) * scale)));
        int nh = std::max(1, static_cast<int>(std::round(static_cast<float>(h) * scale)));
        std::vector<float> a_res;
        ResizeLanczosGrayFloat(a_u8.data(), w, h, a_res, nw, nh);
        int ox = (256 - nw) / 2;
        int oy = (256 - nh) / 2;
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                uint8_t av = static_cast<uint8_t>(std::clamp(std::round(a_res[static_cast<size_t>(y) * nw + x]), 0.0f, 255.0f));
                if (av > 0 && ox + x >= 0 && ox + x < 256 && oy + y >= 0 && oy + y < 256) {
                    uint8_t* dp = out.pixel_ptr(ox + x, oy + y);
                    dp[0] = 128; dp[1] = 128; dp[2] = 128; dp[3] = av;
                }
            }
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            uint8_t av = a_u8[i];
            if (av > 0) {
                out.px[i * 4u + 0] = 128;
                out.px[i * 4u + 1] = 128;
                out.px[i * 4u + 2] = 128;
                out.px[i * 4u + 3] = av;
            }
        }
    }
    return out;
}

static TexImgRgba BoostWeaponIconForHangarAtlas(const TexImgRgba& im_weap) {
    TexImgRgba out = im_weap;
    const size_t n = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
    for (size_t i = 0; i < n; ++i) {
        float a = static_cast<float>(out.px[i * 4u + 3]);
        float a_boost = (a <= 62.0f)
                            ? (a * (183.0f / 62.0f))
                            : (183.0f + (a - 62.0f) * (255.0f - 183.0f) / (255.0f - 62.0f));
        uint8_t av = static_cast<uint8_t>(std::clamp(std::round(a_boost), 0.0f, 255.0f));
        out.px[i * 4u + 3] = av;
        if (av > 0) {
            out.px[i * 4u + 0] = 128;
            out.px[i * 4u + 1] = 128;
            out.px[i * 4u + 2] = 128;
        } else {
            out.px[i * 4u + 0] = 0;
            out.px[i * 4u + 1] = 0;
            out.px[i * 4u + 2] = 0;
        }
    }
    return out;
}

static TexImgRgba ProcessUxAtlas(const TexImgRgba& src) {
    TexImgRgba img = src;
    Ps2AlphaToPc(img);
    const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < n; ++i) {
        img.px[i * 4u + 0] = 105;
        img.px[i * 4u + 1] = 105;
        img.px[i * 4u + 2] = 105;
    }
    return img;
}

static TexImgRgba ProcessIntroGlyphTile(const TexImgRgba& src, uint8_t target_rgb) {
    TexImgRgba img = src;
    Ps2AlphaToPc(img);
    const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = img.px[i * 4u + 3];
        if (a > 0) {
            img.px[i * 4u + 0] = target_rgb;
            img.px[i * 4u + 1] = target_rgb;
            img.px[i * 4u + 2] = target_rgb;
        } else {
            img.px[i * 4u + 0] = 0;
            img.px[i * 4u + 1] = 0;
            img.px[i * 4u + 2] = 0;
        }
    }
    return img;
}

static TexImgRgba ProcessSquadronEmblem(const TexImgRgba& src, const EmblemSpec& spec, bool align_bbox = true) {
    TexImgRgba im_pc = src;
    Ps2AlphaToPc(im_pc);
    if (!align_bbox || im_pc.width != 512 || im_pc.height != 512) {
        return im_pc;
    }
    int target_w = spec.dx1 - spec.dx0;
    int target_h = spec.dy1 - spec.dy0;
    TexImgRgba cropped = CropImage(im_pc, spec.sx0, spec.sy0, spec.sx1, spec.sy1);
    TexImgRgba resized = ResizePremultipliedLanczos(cropped, target_w, target_h);
    TexImgRgba canvas(512, 512);
    PasteImage(canvas, resized, spec.dx0, spec.dy0);
    return canvas;
}

static TexImgRgba ProcessAc5DescriptionDiagram(const TexImgRgba& src, const DescClusterSpec& spec) {
    if (src.empty()) return {};
    const size_t n = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
    std::vector<float> a(n, 0.0f);
    std::vector<float> nz;
    nz.reserve(n / 4);
    for (size_t i = 0; i < n; ++i) {
        float af = static_cast<float>(src.px[i * 4u + 3]);
        a[i] = af;
        if (af > 0.0f) nz.push_back(af);
    }
    if (!nz.empty()) {
        float p80 = ComputePercentile(nz, 80.0f);
        if (p80 > 0.0f) {
            for (size_t i = 0; i < n; ++i) {
                a[i] = std::min(255.0f, a[i] * (255.0f / p80) * 1.15f);
            }
        }
    }
    std::vector<uint8_t> a_u8(n, 0);
    for (size_t i = 0; i < n; ++i) {
        a_u8[i] = static_cast<uint8_t>(std::clamp(std::round(a[i]), 0.0f, 255.0f));
    }

    int cx0 = spec.sx0, cy0 = spec.sy0, cx1 = spec.sx1, cy1 = spec.sy1;
    if (src.width != 512 || src.height != 512) {
        float scale_x = static_cast<float>(src.width) / 80.0f;
        float scale_y = static_cast<float>(src.height) / 64.0f;
        cx0 = std::max(0, static_cast<int>(std::round((spec.dx0 / 8.0f) * scale_x)));
        cy0 = std::max(0, static_cast<int>(std::round((spec.dy0 / 8.0f) * scale_y)));
        cx1 = std::min(src.width, static_cast<int>(std::round((spec.dx1 / 8.0f) * scale_x)));
        cy1 = std::min(src.height, static_cast<int>(std::round((spec.dy1 / 8.0f) * scale_y)));
    }
    cx0 = std::clamp(cx0, 0, src.width);
    cy0 = std::clamp(cy0, 0, src.height);
    cx1 = std::clamp(cx1, cx0, src.width);
    cy1 = std::clamp(cy1, cy0, src.height);

    int cw = std::max(1, cx1 - cx0);
    int ch = std::max(1, cy1 - cy0);
    std::vector<uint8_t> cropped_a(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0);
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            cropped_a[static_cast<size_t>(y) * cw + x] =
                a_u8[static_cast<size_t>(cy0 + y) * src.width + (cx0 + x)];
        }
    }

    int target_w = std::max(1, spec.dx1 - spec.dx0);
    int target_h = std::max(1, spec.dy1 - spec.dy0);
    std::vector<float> res_a;
    ResizeLanczosGrayFloat(cropped_a.data(), cw, ch, res_a, target_w, target_h);

    TexImgRgba out(target_w, target_h);
    for (size_t i = 0; i < res_a.size(); ++i) {
        float v = std::min(255.0f, res_a[i] * 1.25f);
        uint8_t av = static_cast<uint8_t>(std::clamp(std::round(v), 0.0f, 255.0f));
        if (av > 0) {
            out.px[i * 4u + 0] = 128;
            out.px[i * 4u + 1] = 128;
            out.px[i * 4u + 2] = 128;
            out.px[i * 4u + 3] = av;
        }
    }
    return out;
}

// ============================================================================
// 6. Master Font Atlas Builder (1:1 Port of build_master_font_atlases)
// ============================================================================

static TexImgRgba CleanRestoreAndCenter1xTile(const TexImgRgba& cell_rgba, int cp, int adv_x, int height) {
    const int dim = cell_rgba.height;
    if (dim <= 0 || cell_rgba.width != dim) return cell_rgba;
    if (cp == 0x20) return TexImgRgba(dim, dim);

    TexImgRgba work = cell_rgba;
    uint8_t max_a = 0;
    int xs0_min = dim, xs0_max = -1;
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            uint8_t a = work.pixel_ptr(x, y)[3];
            if (a > max_a) max_a = a;
            if (a > 0) {
                if (x < xs0_min) xs0_min = x;
                if (x > xs0_max) xs0_max = x;
            }
        }
    }
    if (max_a == 0) return work;
    if (xs0_min >= 2 && xs0_max <= dim - 3) return work;

    int search_start = std::min(dim - 2, std::max(4, adv_x - 1));
    for (int x = search_start; x < dim; ++x) {
        uint8_t col_max = 0;
        for (int y = 0; y < dim; ++y) {
            col_max = std::max(col_max, work.pixel_ptr(x, y)[3]);
        }
        if (col_max == 0) {
            for (int y = 0; y < dim; ++y) {
                for (int xx = x + 1; xx < dim; ++xx) {
                    uint8_t* p = work.pixel_ptr(xx, y);
                    p[0] = p[1] = p[2] = p[3] = 0;
                }
            }
            break;
        }
    }
    if (adv_x + 6 < dim) {
        for (int y = 0; y < dim; ++y) {
            for (int xx = adv_x + 6; xx < dim; ++xx) {
                uint8_t* p = work.pixel_ptr(xx, y);
                p[0] = p[1] = p[2] = p[3] = 0;
            }
        }
    }

    max_a = 0;
    std::vector<float> fringe_r, fringe_g, fringe_b;
    uint8_t col0_max = 0;
    for (int y = 0; y < dim; ++y) {
        col0_max = std::max(col0_max, work.pixel_ptr(0, y)[3]);
        for (int x = 0; x < dim; ++x) {
            const uint8_t* p = work.pixel_ptr(x, y);
            if (p[3] > max_a) max_a = p[3];
            if (p[3] > 0 && p[3] <= 90) {
                fringe_r.push_back(static_cast<float>(p[0]));
                fringe_g.push_back(static_cast<float>(p[1]));
                fringe_b.push_back(static_cast<float>(p[2]));
            }
        }
    }
    if (max_a == 0) return work;

    const int max_halo_w = (dim >= 32) ? 3 : 2;
    TexImgRgba ext(dim + max_halo_w, dim);
    PasteImage(ext, work, max_halo_w, 0);

    if (!fringe_r.empty() && col0_max > 8) {
        uint8_t halo_r = static_cast<uint8_t>(std::clamp(std::round(ComputePercentile(fringe_r, 50.0f)), 0.0f, 255.0f));
        uint8_t halo_g = static_cast<uint8_t>(std::clamp(std::round(ComputePercentile(fringe_g, 50.0f)), 0.0f, 255.0f));
        uint8_t halo_b = static_cast<uint8_t>(std::clamp(std::round(ComputePercentile(fringe_b, 50.0f)), 0.0f, 255.0f));

        std::vector<std::vector<int>> right_halo_vals;
        for (int y = 0; y < dim; ++y) {
            int rx = -1;
            for (int x = 0; x < dim; ++x) {
                if (work.pixel_ptr(x, y)[3] > 120) rx = x;
            }
            if (rx >= 0) {
                std::vector<int> tail;
                for (int d = 1; d <= max_halo_w; ++d) {
                    if (rx + d < dim && work.pixel_ptr(rx + d, y)[3] > 0) {
                        tail.push_back(static_cast<int>(work.pixel_ptr(rx + d, y)[3]));
                    }
                }
                if (!tail.empty()) right_halo_vals.push_back(std::move(tail));
            }
        }

        int avg_tail[3] = {24, (dim >= 32) ? 14 : 10, 6};
        if (!right_halo_vals.empty()) {
            for (int d = 0; d < max_halo_w; ++d) {
                int sum = 0, cnt = 0;
                for (const auto& t : right_halo_vals) {
                    if (static_cast<int>(t.size()) > d) {
                        sum += t[d];
                        cnt++;
                    }
                }
                avg_tail[d] = (cnt > 0) ? static_cast<int>(std::round(static_cast<float>(sum) / cnt)) : 0;
            }
        }

        for (int y = 0; y < dim; ++y) {
            int a0 = work.pixel_ptr(0, y)[3];
            if (a0 <= 8) continue;
            if (a0 > 120) {
                for (int d = 1; d <= max_halo_w; ++d) {
                    int ha = avg_tail[d - 1];
                    if (ha > 0) {
                        uint8_t* ep = ext.pixel_ptr(max_halo_w - d, y);
                        ep[0] = halo_r; ep[1] = halo_g; ep[2] = halo_b;
                        ep[3] = static_cast<uint8_t>(ha);
                    }
                }
            } else {
                int cur_a = a0;
                for (int d = 1; d <= max_halo_w; ++d) {
                    cur_a = static_cast<int>(std::round(cur_a * 0.55f));
                    if (cur_a >= 6) {
                        uint8_t* ep = ext.pixel_ptr(max_halo_w - d, y);
                        ep[0] = halo_r; ep[1] = halo_g; ep[2] = halo_b;
                        ep[3] = static_cast<uint8_t>(cur_a);
                    }
                }
            }
        }
    }

    int xs_min = ext.width, xs_max = -1, ys_min = dim, ys_max = -1;
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < ext.width; ++x) {
            if (ext.pixel_ptr(x, y)[3] > 0) {
                if (x < xs_min) xs_min = x;
                if (x > xs_max) xs_max = x;
                if (y < ys_min) ys_min = y;
                if (y > ys_max) ys_max = y;
            }
        }
    }
    if (xs_max < xs_min) return TexImgRgba(dim, dim);

    int orig_x0 = xs_min - max_halo_w;
    int orig_x1 = xs_max - max_halo_w + 1;
    int orig_y0 = ys_min;
    int orig_y1 = ys_max + 1;
    int pad_x = (dim - std::min(dim, adv_x)) / 2;
    int pad_y = (dim - std::min(dim, height)) / 2;
    if (orig_x0 + pad_x < 1) pad_x = 1 - orig_x0;
    if (orig_x1 + pad_x > dim - 1) pad_x = std::max(0 - orig_x0, (dim - 1) - orig_x1);
    if (orig_y0 + pad_y < 1) pad_y = 1 - orig_y0;
    if (orig_y1 + pad_y > dim - 1) pad_y = std::max(0 - orig_y0, (dim - 1) - orig_y1);

    TexImgRgba out(dim, dim);
    for (int y = 0; y < dim; ++y) {
        int dy = y + pad_y;
        if (dy < 0 || dy >= dim) continue;
        for (int x = -max_halo_w; x < dim; ++x) {
            const uint8_t* sp = ext.pixel_ptr(x + max_halo_w, y);
            if (sp[3] == 0) continue;
            int dx = x + pad_x;
            if (dx >= 0 && dx < dim) {
                uint8_t* dp = out.pixel_ptr(dx, dy);
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
    }
    return out;
}

static TexImgRgba CenterHdGlyphTile(const TexImgRgba& tile_im, int dim, int adv_x, int height) {
    if (tile_im.empty() || dim <= 0) return tile_im;
    const int tw = tile_im.width;
    const int th = tile_im.height;
    const int scale = std::max(1, tw / dim);
    int x0 = tw, x1 = -1, y0 = th, y1 = -1;
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            if (tile_im.pixel_ptr(x, y)[3] > 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < x0) return tile_im;
    x1 += 1;
    y1 += 1;
    if (x0 >= 2 * scale && x1 <= tw - 2 * scale) return tile_im;

    int pad_x_hd = ((dim - std::min(dim, adv_x)) / 2) * scale;
    int pad_y_hd = ((dim - std::min(dim, height)) / 2) * scale;
    if (x0 + pad_x_hd < scale) pad_x_hd = scale - x0;
    if (x1 + pad_x_hd > tw - scale) pad_x_hd = std::max(0 - x0, (tw - scale) - x1);
    if (y0 + pad_y_hd < scale) pad_y_hd = scale - y0;
    if (y1 + pad_y_hd > th - scale) pad_y_hd = std::max(0 - y0, (th - scale) - y1);

    TexImgRgba out(tw, th);
    for (int sy = y0; sy < y1; ++sy) {
        int dy = sy + pad_y_hd;
        if (dy >= 0 && dy < th) {
            for (int sx = x0; sx < x1; ++sx) {
                int dx = sx + pad_x_hd;
                if (dx >= 0 && dx < tw) {
                    const uint8_t* sp = tile_im.pixel_ptr(sx, sy);
                    uint8_t* dp = out.pixel_ptr(dx, dy);
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
                }
            }
        }
    }
    return out;
}

struct MasterGlyphEntry {
    int idx = 0;
    int orig_idx = 0;
    int col = 0;
    int row = 0;
    int cp = 0;
    std::string ch;
    int adv_x = 0;
    int height = 0;
    int std_h = 0;
    std::string tile_hash;
    std::string exact_hash;
    std::string family;
    int dim = 32;
    std::vector<std::string> alt_hashes;
    TexImgRgba tile_1x;
    TexImgRgba ostrich_im;
};

struct MasterSectionMeta {
    int blocks = 1;
    std::string sample;
};

struct MasterOutSection {
    std::string family;
    int dim = 32;
    int start = 0;
    int end = 0;
    int count = 0;
    int blocks = 1;
    std::string sample;
};

static void AddUniqueHash(std::vector<std::string>& vec, const std::string& h, const std::string& th, const std::string& eh) {
    if (h.size() != 16 || h == th || h == eh) return;
    for (const auto& existing : vec) {
        if (existing == h) return;
    }
    vec.push_back(h);
}

static void ParseMasterTxtBytes(
    const std::vector<uint8_t>& txt_bytes,
    int default_dim,
    std::vector<MasterGlyphEntry>& out_entries,
    std::unordered_map<std::string, MasterSectionMeta>& out_sec_meta)
{
    out_entries.clear();
    out_sec_meta.clear();
    if (txt_bytes.empty()) return;

    std::unordered_map<std::string, std::string> runtime_16_map;
    for (const auto& p : kRuntime16x16AltExactHashes) {
        runtime_16_map[p.k] = p.v;
    }

    std::string content(reinterpret_cast<const char*>(txt_bytes.data()), txt_bytes.size());
    std::istringstream iss(content);
    std::string raw;
    int mode = 0; // 0=header, 1=sections, 2=samples, 3=glyphs

    while (std::getline(iss, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.find("TYPEFACE SECTIONS IN THIS MASTER ATLAS") != std::string::npos) {
            mode = 1;
            continue;
        } else if (raw.find("SAMPLE STRINGS FROM SEEN FONTS:") != std::string::npos) {
            mode = 2;
            continue;
        } else if (raw.find("GLYPH METRICS") != std::string::npos) {
            mode = 3;
            continue;
        }

        if (mode == 1) {
            size_t fam_pos = raw.find("Family=");
            size_t blk_pos = raw.find("blocks)");
            size_t smp_pos = raw.find("Sample: \"");
            if (fam_pos != std::string::npos && fam_pos + 23 <= raw.size() && blk_pos != std::string::npos) {
                std::string fam = ToUpperAscii(raw.substr(fam_pos + 7, 16));
                size_t comma = raw.rfind(',', blk_pos);
                int blocks = 1;
                if (comma != std::string::npos) {
                    blocks = std::max(1, std::atoi(raw.c_str() + comma + 1));
                }
                std::string sample;
                if (smp_pos != std::string::npos) {
                    size_t q_end = raw.rfind('"');
                    if (q_end != std::string::npos && q_end > smp_pos + 9) {
                        sample = raw.substr(smp_pos + 9, q_end - (smp_pos + 9));
                    }
                }
                out_sec_meta[fam] = {blocks, sample};
            }
        } else if (mode == 3) {
            // Example:
            //   Glyph #000 (col=0, row=0): U+0020 ' ' | adv_x=10, height=25 | TileHash=90FC0AC271251613 | ExactHash=89AB... | Family=1C85B763AD07A4EC
            size_t g_pos = raw.find("Glyph #");
            if (g_pos == std::string::npos) continue;
            size_t col_pos = raw.find("(col=", g_pos);
            size_t row_pos = raw.find("row=", col_pos);
            size_t u_pos   = raw.find("): U+", row_pos);
            size_t adv_pos = raw.find(" | adv_x=", u_pos);
            size_t h_pos   = raw.find("height=", adv_pos);
            size_t th_pos  = raw.find("TileHash=", h_pos);
            size_t eh_pos  = raw.find("ExactHash=", th_pos);
            size_t fm_pos  = raw.find("Family=", eh_pos);
            if (col_pos == std::string::npos || row_pos == std::string::npos || u_pos == std::string::npos ||
                adv_pos == std::string::npos || h_pos == std::string::npos || th_pos == std::string::npos ||
                eh_pos == std::string::npos || fm_pos == std::string::npos) {
                continue;
            }
            MasterGlyphEntry e;
            e.idx      = std::atoi(raw.c_str() + g_pos + 7);
            e.orig_idx = e.idx;
            e.col      = std::atoi(raw.c_str() + col_pos + 5);
            e.row      = std::atoi(raw.c_str() + row_pos + 4);
            e.cp       = static_cast<int>(std::strtoul(raw.c_str() + u_pos + 5, nullptr, 16));

            size_t q1 = raw.find('\'', u_pos + 9);
            if (q1 != std::string::npos && q1 < adv_pos && adv_pos >= 1 && raw[adv_pos - 1] == '\'') {
                e.ch = raw.substr(q1 + 1, (adv_pos - 1) - (q1 + 1));
            }
            e.adv_x      = std::atoi(raw.c_str() + adv_pos + 9);
            e.height     = std::atoi(raw.c_str() + h_pos + 7);
            e.tile_hash  = ToUpperAscii(raw.substr(th_pos + 9, 16));
            e.exact_hash = ToUpperAscii(raw.substr(eh_pos + 10, 16));
            e.family     = ToUpperAscii(raw.substr(fm_pos + 7, 16));
            e.dim        = default_dim;

            size_t dim_pos = raw.find("Dim=");
            if (dim_pos != std::string::npos) {
                int dv = std::atoi(raw.c_str() + dim_pos + 4);
                if (dv > 0) e.dim = dv;
            }
            size_t alt_pos = raw.find("AltHashes=");
            if (alt_pos != std::string::npos) {
                size_t start = alt_pos + 10;
                size_t end = raw.find(' ', start);
                std::string alist = (end == std::string::npos) ? raw.substr(start) : raw.substr(start, end - start);
                size_t p = 0;
                while (p < alist.size()) {
                    size_t c = alist.find(',', p);
                    std::string tok = TrimAscii((c == std::string::npos) ? alist.substr(p) : alist.substr(p, c - p));
                    tok = ToUpperAscii(tok);
                    AddUniqueHash(e.alt_hashes, tok, e.tile_hash, e.exact_hash);
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
            }
            auto rit = runtime_16_map.find(e.tile_hash);
            if (rit != runtime_16_map.end()) {
                AddUniqueHash(e.alt_hashes, rit->second, e.tile_hash, e.exact_hash);
            }
            out_entries.push_back(std::move(e));
        }
    }
}

static void DeduplicateVisualFamilies(
    const std::vector<MasterGlyphEntry>& entries,
    std::unordered_map<std::string, MasterSectionMeta>& sec_meta,
    std::vector<MasterGlyphEntry>& out_entries,
    std::vector<MasterOutSection>& out_sections)
{
    out_entries.clear();
    out_sections.clear();

    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<MasterGlyphEntry>> fams;
    for (const auto& e : entries) {
        if (fams.find(e.family) == fams.end()) {
            order.push_back(e.family);
        }
        fams[e.family].push_back(e);
    }

    std::unordered_set<std::string> merged_into;
    std::vector<std::string> kept_order;

    for (size_t i = 0; i < order.size(); ++i) {
        const std::string& fa = order[i];
        if (merged_into.find(fa) != merged_into.end()) continue;
        kept_order.push_back(fa);

        auto& fa_list = fams[fa];
        std::unordered_map<int, size_t> ga_by_cp;
        for (size_t k = 0; k < fa_list.size(); ++k) {
            ga_by_cp[fa_list[k].cp] = k;
        }

        for (size_t j = i + 1; j < order.size(); ++j) {
            const std::string& fb = order[j];
            if (merged_into.find(fb) != merged_into.end()) continue;
            auto& fb_list = fams[fb];
            if (fa_list.empty() || fb_list.empty() || fa_list[0].dim != fb_list[0].dim) continue;

            std::vector<size_t> common_b_indices;
            for (size_t k = 0; k < fb_list.size(); ++k) {
                if (fb_list[k].cp > 0x20 && ga_by_cp.find(fb_list[k].cp) != ga_by_cp.end()) {
                    common_b_indices.push_back(k);
                }
            }
            if (common_b_indices.empty()) continue;

            int sim = 0;
            for (size_t idx_b : common_b_indices) {
                const auto& g_b = fb_list[idx_b];
                const auto& g_a = fa_list[ga_by_cp[g_b.cp]];
                if (std::abs(g_a.adv_x - g_b.adv_x) <= 1 && std::abs(g_a.height - g_b.height) <= 1) {
                    if (!g_a.tile_1x.empty() && g_a.tile_1x.px.size() == g_b.tile_1x.px.size()) {
                        int max_diff = 0;
                        for (size_t pi = 0; pi < g_a.tile_1x.px.size(); ++pi) {
                            int d = std::abs(static_cast<int>(g_a.tile_1x.px[pi]) - static_cast<int>(g_b.tile_1x.px[pi]));
                            if (d > max_diff) max_diff = d;
                        }
                        if (max_diff <= 32) sim++;
                    }
                }
            }

            if (sim >= 1 && sim * 4 >= static_cast<int>(common_b_indices.size()) * 3) {
                merged_into.insert(fb);
                sec_meta[fa].blocks += std::max(1, sec_meta[fb].blocks);
                for (const auto& g_b : fb_list) {
                    auto it_a = ga_by_cp.find(g_b.cp);
                    if (it_a != ga_by_cp.end()) {
                        auto& g_a = fa_list[it_a->second];
                        AddUniqueHash(g_a.alt_hashes, g_b.tile_hash, g_a.tile_hash, g_a.exact_hash);
                        AddUniqueHash(g_a.alt_hashes, g_b.exact_hash, g_a.tile_hash, g_a.exact_hash);
                        for (const auto& ah : g_b.alt_hashes) {
                            AddUniqueHash(g_a.alt_hashes, ah, g_a.tile_hash, g_a.exact_hash);
                        }
                    } else {
                        MasterGlyphEntry g_copy = g_b;
                        g_copy.family = fa;
                        fa_list.push_back(std::move(g_copy));
                    }
                }
                std::sort(fa_list.begin(), fa_list.end(),
                          [](const MasterGlyphEntry& a, const MasterGlyphEntry& b) {
                              if (a.cp != b.cp) return a.cp < b.cp;
                              if (a.exact_hash != b.exact_hash) return a.exact_hash < b.exact_hash;
                              return a.tile_hash < b.tile_hash;
                          });
                ga_by_cp.clear();
                for (size_t k = 0; k < fa_list.size(); ++k) {
                    ga_by_cp[fa_list[k].cp] = k;
                }
            }
        }
    }

    for (const auto& fa : kept_order) {
        int s0 = static_cast<int>(out_entries.size());
        const auto& flist = fams[fa];
        for (const auto& item : flist) out_entries.push_back(item);
        int s1 = static_cast<int>(out_entries.size()) - 1;
        auto meta = sec_meta[fa];
        if (meta.blocks <= 0) meta.blocks = 1;
        out_sections.push_back({fa, flist.empty() ? 32 : flist[0].dim, s0, s1, static_cast<int>(flist.size()), meta.blocks, meta.sample});
    }
}

static int ComputeModeHeight(const std::vector<MasterGlyphEntry>& elist, int fallback_h) {
    std::vector<int> src_h;
    for (const auto& x : elist) {
        bool is_alnum = (x.cp >= '0' && x.cp <= '9') || (x.cp >= 'A' && x.cp <= 'Z') || (x.cp >= 'a' && x.cp <= 'z');
        if (is_alnum && x.height > 0) src_h.push_back(x.height);
    }
    if (src_h.empty()) {
        for (const auto& x : elist) {
            if (x.height > 0) src_h.push_back(x.height);
        }
    }
    if (src_h.empty()) return fallback_h;
    int best_h = src_h[0], best_cnt = 0;
    for (int v : src_h) {
        int c = 0;
        for (int u : src_h) if (u == v) c++;
        if (c > best_cnt || (c == best_cnt && v > best_h)) {
            best_cnt = c;
            best_h = v;
        }
    }
    return best_h;
}

struct MasterFontBuildResult {
    int cnt_atlases = 0;
    int cnt_glyphs = 0;
    int cnt_font_json = 0;
};

#ifdef _WIN32
static MasterFontBuildResult BuildMasterFontAtlases(
    const std::unordered_map<std::string, std::vector<uint8_t>>& master_font_files,
    const std::unordered_map<std::string, std::vector<uint8_t>>& intro_letterhead,
    const std::unordered_map<std::string, std::vector<uint8_t>>& intro_upper,
    const std::unordered_map<std::string, std::vector<uint8_t>>& intro_lower,
    const fs::path& out_fonts_dir)
{
    MasterFontBuildResult res;
    if (master_font_files.empty()) return res;

    std::error_code ec;
    fs::create_directories(out_fonts_dir, ec);

    std::unordered_map<std::string, const std::vector<uint8_t>*> ci_map;
    for (const auto& kv : master_font_files) {
        ci_map[ToLowerAscii(kv.first)] = &kv.second;
    }

    // Remove legacy standalone MASTER_EN_font_16x16.png / .txt / .json if left over
    for (const char* old_16 : {"MASTER_EN_font_16x16.png", "MASTER_EN_font_16x16.txt", "MASTER_EN_font_16x16.json"}) {
        fs::remove(out_fonts_dir / old_16, ec);
    }

    // ------------------------------------------------------------------------
    // 1. MASTER_EN_font_64x64.png + .txt (8x -> 512x512 centered cells)
    // ------------------------------------------------------------------------
    auto it_png64 = ci_map.find("master_en_font_64x64.png");
    auto it_txt64 = ci_map.find("master_en_font_64x64.txt");
    if (it_png64 != ci_map.end() && it_txt64 != ci_map.end()) {
        std::vector<MasterGlyphEntry> raw_entries_64;
        std::unordered_map<std::string, MasterSectionMeta> sec_meta_64;
        ParseMasterTxtBytes(*it_txt64->second, 64, raw_entries_64, sec_meta_64);

        TexImgRgba im64_base;
        if (DecodePngBytesViaWic(it_png64->second->data(), it_png64->second->size(), im64_base)) {
            TexImgRgba im64_1x = (im64_base.width != 16 * 64 && im64_base.width >= 16)
                ? ResizeNearest(im64_base, 16 * 64, (im64_base.height / (im64_base.width / 16)) * 64)
                : im64_base;

            std::unordered_map<int, MasterGlyphEntry> e64_by_idx;
            for (const auto& e64 : raw_entries_64) {
                TexImgRgba cell1x = CropImage(im64_1x, e64.col * 64, e64.row * 64, (e64.col + 1) * 64, (e64.row + 1) * 64);
                TexImgRgba cleaned = CleanRestoreAndCenter1xTile(cell1x, e64.cp, e64.adv_x, e64.height);
                PasteImage(im64_1x, cleaned, e64.col * 64, e64.row * 64);
                e64_by_idx[e64.orig_idx] = e64;
            }

            TexImgRgba canvas64 = ResizeNearest(im64_1x, im64_1x.width * 8, im64_1x.height * 8);
            for (const auto& lh : kLetterhead64Map) {
                auto it_tile = intro_letterhead.find(ToLowerAscii(lh.fn));
                if (it_tile == intro_letterhead.end()) continue;
                TexImgRgba raw_im;
                if (!DecodePngBytesViaWic(it_tile->second.data(), it_tile->second.size(), raw_im)) continue;
                TexImgRgba tile512 = ProcessIntroGlyphTile(raw_im, 255);
                auto it_e = e64_by_idx.find(lh.glyph_idx);
                if (it_e != e64_by_idx.end()) {
                    tile512 = CenterHdGlyphTile(tile512, 64, it_e->second.adv_x, it_e->second.height);
                }
                int col = lh.glyph_idx % 16;
                int row = lh.glyph_idx / 16;
                PasteImage(canvas64, tile512, col * 512, row * 512);
                res.cnt_glyphs++;
            }

            if (SavePngFileViaWic(out_fonts_dir / "MASTER_EN_font_64x64.png", canvas64)) {
                std::string txt64_str(reinterpret_cast<const char*>(it_txt64->second->data()), it_txt64->second->size());
                if (txt64_str.find("CenteredCells:") == std::string::npos) {
                    size_t pos = txt64_str.find("Grid Columns:          16");
                    if (pos != std::string::npos) {
                        size_t eol = txt64_str.find('\n', pos);
                        if (eol != std::string::npos) {
                            txt64_str.insert(eol + 1, "CenteredCells:         1\n");
                        }
                    }
                }
                WriteWholeFileText(out_fonts_dir / "MASTER_EN_font_64x64.txt", txt64_str);
                res.cnt_atlases++;
            }
        }
    }

    // ------------------------------------------------------------------------
    // 2. MASTER_EN_font_32x32.png + .txt + .json (4x -> 128x128 cells)
    // ------------------------------------------------------------------------
    auto it_png32 = ci_map.find("master_en_font_32x32.png");
    auto it_txt32 = ci_map.find("master_en_font_32x32.txt");
    if (it_png32 != ci_map.end() && it_txt32 != ci_map.end()) {
        std::vector<MasterGlyphEntry> all_entries_32;
        std::unordered_map<std::string, MasterSectionMeta> sec_meta_32;
        ParseMasterTxtBytes(*it_txt32->second, 32, all_entries_32, sec_meta_32);

        std::vector<MasterGlyphEntry> raw_entries_32;
        for (auto& e : all_entries_32) {
            if (e.dim == 32) raw_entries_32.push_back(std::move(e));
        }

        TexImgRgba im32_src;
        if (DecodePngBytesViaWic(it_png32->second->data(), it_png32->second->size(), im32_src) && !raw_entries_32.empty()) {
            const int cols = 16;
            const int cell_hd = 128;
            const int cell32_w = im32_src.width / cols;

            std::unordered_map<std::string, std::vector<MasterGlyphEntry>> by_f32_tmp;
            for (const auto& e : raw_entries_32) by_f32_tmp[e.family].push_back(e);
            std::unordered_map<std::string, int> fam32_std_h;
            for (const auto& kv : by_f32_tmp) {
                fam32_std_h[kv.first] = ComputeModeHeight(kv.second, 32);
            }

            std::unordered_map<int, TexImgRgba> ostrich_by_cp;
            for (const auto& item : kIntroSub32UpperMap) {
                auto it_u = intro_upper.find(ToLowerAscii(item.fn));
                if (it_u == intro_upper.end()) continue;
                TexImgRgba raw_im;
                if (!DecodePngBytesViaWic(it_u->second.data(), it_u->second.size(), raw_im)) continue;
                TexImgRgba tile256 = ProcessIntroGlyphTile(raw_im, 217);
                TexImgRgba tile128 = ResizePremultipliedLanczos(tile256, 128, 128);
                if (item.cp1 >= 0) ostrich_by_cp[item.cp1] = tile128;
                if (item.cp2 >= 0) ostrich_by_cp[item.cp2] = tile128;
            }
            for (const auto& item : kIntroSub32LowerMap) {
                auto it_l = intro_lower.find(ToLowerAscii(item.fn));
                if (it_l == intro_lower.end()) continue;
                TexImgRgba raw_im;
                if (!DecodePngBytesViaWic(it_l->second.data(), it_l->second.size(), raw_im)) continue;
                TexImgRgba tile256 = ProcessIntroGlyphTile(raw_im, 217);
                TexImgRgba tile128 = ResizePremultipliedLanczos(tile256, 128, 128);
                if (item.cp1 >= 0) ostrich_by_cp[item.cp1] = tile128;
                if (item.cp2 >= 0) ostrich_by_cp[item.cp2] = tile128;
            }
            if (!intro_upper.empty() || !intro_lower.empty()) {
                std::vector<uint8_t> raw4 = DecodeBase64(kSynthIntro4_256B64);
                TexImgRgba synth4;
                if (DecodePngBytesViaWic(raw4.data(), raw4.size(), synth4)) {
                    ostrich_by_cp[0x0034] = ResizePremultipliedLanczos(synth4, 128, 128);
                }
            }

            fs::path out_png32_path = out_fonts_dir / "MASTER_EN_font_32x32.png";
            TexImgRgba canvas32;
            std::vector<MasterGlyphEntry> dedup_32;

            if (cell32_w >= 128) {
                if (cell32_w != cell_hd) {
                    int target_rows = im32_src.height / cell32_w;
                    canvas32 = ResizePremultipliedLanczos(im32_src, cols * cell_hd, target_rows * cell_hd);
                } else {
                    canvas32 = im32_src;
                }
                dedup_32 = raw_entries_32;
                for (auto& e : dedup_32) {
                    int std_h_val = fam32_std_h.count(e.family) ? fam32_std_h[e.family] : e.height;
                    e.std_h = std_h_val;
                    if (e.family == "1C85B763AD07A4EC") {
                        auto it_os = ostrich_by_cp.find(e.cp);
                        if (it_os != ostrich_by_cp.end()) {
                            TexImgRgba cell_im = CenterHdGlyphTile(it_os->second, 32, e.adv_x, std_h_val);
                            PasteImage(canvas32, cell_im, e.col * cell_hd, e.row * cell_hd);
                        }
                        res.cnt_glyphs++;
                    }
                }
                if (SavePngFileViaWic(out_png32_path, canvas32)) {
                    std::string txt32_str(reinterpret_cast<const char*>(it_txt32->second->data()), it_txt32->second->size());
                    if (txt32_str.find("CenteredCells:") == std::string::npos) {
                        size_t pos = txt32_str.find("Grid Columns:          16");
                        if (pos != std::string::npos) {
                            size_t eol = txt32_str.find('\n', pos);
                            if (eol != std::string::npos) {
                                txt32_str.insert(eol + 1, "CenteredCells:         1\n");
                            }
                        }
                    }
                    WriteWholeFileText(out_fonts_dir / "MASTER_EN_font_32x32.txt", txt32_str);
                    res.cnt_atlases++;
                }
            } else {
                if (cell32_w != 32 && cell32_w > 0) {
                    im32_src = ResizeNearest(im32_src, 16 * 32, (im32_src.height / cell32_w) * 32);
                }
                for (auto& e : raw_entries_32) {
                    int g = e.orig_idx;
                    int sx0 = (g % cols) * 32;
                    int sy0 = (g / cols) * 32;
                    e.tile_1x = CropImage(im32_src, sx0, sy0, sx0 + 32, sy0 + 32);
                    if (e.family == "1C85B763AD07A4EC") {
                        auto it_os = ostrich_by_cp.find(e.cp);
                        if (it_os != ostrich_by_cp.end()) e.ostrich_im = it_os->second;
                    }
                }
                std::vector<MasterOutSection> secs_32;
                DeduplicateVisualFamilies(raw_entries_32, sec_meta_32, dedup_32, secs_32);
                int total_32 = static_cast<int>(dedup_32.size());
                int rows_32 = (total_32 + cols - 1) / cols;
                canvas32.allocate(cols * cell_hd, rows_32 * cell_hd);

                for (int new_idx = 0; new_idx < total_32; ++new_idx) {
                    auto& e = dedup_32[new_idx];
                    e.idx = new_idx;
                    e.col = new_idx % cols;
                    e.row = new_idx / cols;
                    int std_h_val = fam32_std_h.count(e.family) ? fam32_std_h[e.family] : e.height;
                    e.std_h = std_h_val;
                    TexImgRgba cell_im;
                    if (!e.ostrich_im.empty()) {
                        cell_im = CenterHdGlyphTile(e.ostrich_im, 32, e.adv_x, std_h_val);
                        res.cnt_glyphs++;
                    } else {
                        TexImgRgba cent_1x = CleanRestoreAndCenter1xTile(e.tile_1x, e.cp, e.adv_x, std_h_val);
                        cell_im = ResizeNearest(cent_1x, cell_hd, cell_hd);
                    }
                    PasteImage(canvas32, cell_im, e.col * cell_hd, e.row * cell_hd);
                }

                if (SavePngFileViaWic(out_png32_path, canvas32)) {
                    int total_blocks_32 = 0;
                    for (const auto& s : secs_32) total_blocks_32 += s.blocks;
                    std::ostringstream tss;
                    tss << "====================================================================\n"
                        << " Master Font Atlas [EN] — Unified 32x32 Language Font Atlas (4x HD, Deduplicated)\n"
                        << "====================================================================\n"
                        << "Master Atlas Name:     MASTER_EN_font_32x32\n"
                        << "Language:              EN\n"
                        << "Glyph Dimension:       32x32\n"
                        << "Grid Columns:          16\n"
                        << "CenteredCells:         1\n"
                        << "Collected Glyphs:      " << total_32 << " (32x32, 16 columns)\n"
                        << "Member Font Blocks:    " << total_blocks_32 << "\n"
                        << "To replace all 32x32 (and linked 16x16) fonts in-game:\n"
                        << "  Copy edited MASTER_EN_font_32x32.png, .txt, AND .json into mods\\textures\\\n"
                        << "--------------------------------------------------------------------\n"
                        << "TYPEFACE SECTIONS IN THIS MASTER ATLAS:\n";
                    for (const auto& s : secs_32) {
                        char sbuf[512];
                        snprintf(sbuf, sizeof(sbuf),
                                 "  * Glyphs #%03d..#%03d (rows %d..%d): Family=%s (%d unique glyphs, %d blocks) | Sample: \"%s\"\n",
                                 s.start, s.end, s.start / cols, s.end / cols,
                                 s.family.c_str(), s.count, s.blocks, s.sample.c_str());
                        tss << sbuf;
                    }
                    tss << "--------------------------------------------------------------------\n"
                        << "GLYPH METRICS (Grid 16 columns):\n";
                    for (auto& e : dedup_32) {
                        std::sort(e.alt_hashes.begin(), e.alt_hashes.end());
                        std::string alt_str;
                        if (!e.alt_hashes.empty()) {
                            alt_str = " | AltHashes=";
                            for (size_t ai = 0; ai < e.alt_hashes.size(); ++ai) {
                                if (ai > 0) alt_str += ",";
                                alt_str += e.alt_hashes[ai];
                            }
                        }
                        char gbuf[512];
                        snprintf(gbuf, sizeof(gbuf),
                                 "  Glyph #%03d (col=%d, row=%d): U+%04X '%s' | adv_x=%d, height=%d | TileHash=%s | ExactHash=%s | Family=%s%s\n",
                                 e.idx, e.col, e.row, e.cp, e.ch.c_str(), e.adv_x, e.height,
                                 e.tile_hash.c_str(), e.exact_hash.c_str(), e.family.c_str(), alt_str.c_str());
                        tss << gbuf;
                    }
                    WriteWholeFileText(out_fonts_dir / "MASTER_EN_font_32x32.txt", tss.str());
                    res.cnt_atlases++;
                }
            }

            // Subtitle Brackets GIM BB10DED7AB6393C6 (from cells #701..#703 at row 43, cols 13..15)
            if (canvas32.height >= 44 * cell_hd && canvas32.width >= 16 * cell_hd) {
                TexImgRgba gim_sb(cell_hd, cell_hd);
                int sc = cell_hd / 32;
                // #701 (col=13, row=43): '<' (11x25 at 10,3 -> 0,0)
                PasteImage(gim_sb,
                           CropImage(canvas32, 13 * cell_hd + 10 * sc, 43 * cell_hd + 3 * sc,
                                     13 * cell_hd + 21 * sc, 43 * cell_hd + 28 * sc),
                           0, 0);
                // #702 (col=14, row=43): '[' (9x25 at 11,3 -> 11,0)
                PasteImage(gim_sb,
                           CropImage(canvas32, 14 * cell_hd + 11 * sc, 43 * cell_hd + 3 * sc,
                                     14 * cell_hd + 20 * sc, 43 * cell_hd + 28 * sc),
                           11 * sc, 0);
                // #703 (col=15, row=43): '-' (15x7 at 8,13 -> 0,25)
                PasteImage(gim_sb,
                           CropImage(canvas32, 15 * cell_hd + 8 * sc, 43 * cell_hd + 13 * sc,
                                     15 * cell_hd + 23 * sc, 43 * cell_hd + 20 * sc),
                           0, 25 * sc);
                bool any_alpha = false;
                for (size_t i = 3; i < gim_sb.px.size(); i += 4) {
                    if (gim_sb.px[i] > 0) { any_alpha = true; break; }
                }
                if (any_alpha) {
                    SavePngFileViaWic(out_fonts_dir / "subtitle_brackets_BB10DED7AB6393C6.png", gim_sb);
                }
            }

            // 3. Combine MASTER_EN_font_16x16 into MASTER_EN_font_32x32.json
            auto it_png16 = ci_map.find("master_en_font_16x16.png");
            auto it_txt16 = ci_map.find("master_en_font_16x16.txt");
            if (it_png16 != ci_map.end() && it_txt16 != ci_map.end()) {
                std::vector<MasterGlyphEntry> raw_entries_16;
                std::unordered_map<std::string, MasterSectionMeta> sec_meta_16;
                ParseMasterTxtBytes(*it_txt16->second, 16, raw_entries_16, sec_meta_16);
                TexImgRgba im16_src;
                if (DecodePngBytesViaWic(it_png16->second->data(), it_png16->second->size(), im16_src) && !raw_entries_16.empty()) {
                    int cell16_w = im16_src.width / 16;
                    if (cell16_w != 16 && cell16_w > 0) {
                        im16_src = ResizeNearest(im16_src, 16 * 16, (im16_src.height / cell16_w) * 16);
                    }
                    for (auto& e16 : raw_entries_16) {
                        int g = e16.orig_idx;
                        int sx0 = (g % cols) * 16;
                        int sy0 = (g / cols) * 16;
                        e16.tile_1x = CropImage(im16_src, sx0, sy0, sx0 + 16, sy0 + 16);
                    }
                    std::vector<MasterGlyphEntry> dedup_16;
                    std::vector<MasterOutSection> secs_16;
                    DeduplicateVisualFamilies(raw_entries_16, sec_meta_16, dedup_16, secs_16);

                    std::unordered_map<std::string, std::unordered_map<int, const MasterGlyphEntry*>> by_fam32;
                    for (const auto& g32 : dedup_32) {
                        by_fam32[g32.family][g32.cp] = &g32;
                    }

                    std::unordered_map<std::string, std::vector<MasterGlyphEntry>> by_f16_tmp;
                    for (const auto& e16 : dedup_16) by_f16_tmp[e16.family].push_back(e16);
                    std::unordered_map<std::string, int> fam16_std_h;
                    for (const auto& kv : by_f16_tmp) {
                        fam16_std_h[kv.first] = ComputeModeHeight(kv.second, 13);
                    }

                    auto get_prio = [](const std::string& fam16) -> std::vector<std::string> {
                        if (fam16 == "A9910AD20F17AD1A") {
                            return {"B1A1FCD42EF604EC", "10C596C6BDDD728C", "580E0574D95B2442", "B986D279B830A42E"};
                        }
                        if (fam16 == "12C5663260CB8DB3" || fam16 == "0FC97B55D270F000") {
                            return {"580E0574D95B2442", "B1A1FCD42EF604EC", "B986D279B830A42E"};
                        }
                        return {"580E0574D95B2442", "B1A1FCD42EF604EC", "10C596C6BDDD728C", "B986D279B830A42E"};
                    };

                    std::ostringstream jss;
                    jss << "{\n"
                        << "  \"comment\": \"Combined MASTER_EN_font_32x32 (4x, 128x128 centered cells) + MASTER_EN_font_16x16 + Subtitle Brackets GIM BB10DED7AB6393C6\",\n"
                        << "  \"upscale_factor\": 4,\n"
                        << "  \"cell_size\": " << cell_hd << ",\n"
                        << "  \"centered_cells\": true,\n"
                        << "  \"merged_16x16_master\": \"MASTER_EN_font_16x16\",\n"
                        << "  \"subtitle_brackets_gim\": {\n"
                        << "    \"gim_hash\": \"BB10DED7AB6393C6\",\n"
                        << "    \"cells\": [\n"
                        << "      {\"cell\": 701, \"symbol\": \"<\", \"src_1x\": [10, 3, 11, 25], \"dst_1x\": [0, 0, 11, 25]},\n"
                        << "      {\"cell\": 702, \"symbol\": \"[\", \"src_1x\": [11, 3, 9, 25], \"dst_1x\": [11, 0, 9, 25]},\n"
                        << "      {\"cell\": 703, \"symbol\": \"-\", \"src_1x\": [8, 13, 15, 7], \"dst_1x\": [0, 25, 15, 7]}\n"
                        << "    ]\n"
                        << "  },\n"
                        << "  \"hashes\": [\"MASTER_EN_font_32x32\", \"MASTER_EN_font_16x16\"],\n"
                        << "  \"glyphs_16x16\": [\n";

                    bool first_g = true;
                    for (size_t idx16 = 0; idx16 < dedup_16.size(); ++idx16) {
                        auto& e16 = dedup_16[idx16];
                        const MasterGlyphEntry* donor = nullptr;
                        for (const auto& dfam : get_prio(e16.family)) {
                            auto it_f = by_fam32.find(dfam);
                            if (it_f != by_fam32.end()) {
                                auto it_c = it_f->second.find(e16.cp);
                                if (it_c != it_f->second.end()) {
                                    donor = it_c->second;
                                    break;
                                }
                            }
                        }
                        if (!donor) {
                            for (const auto& kv : by_fam32) {
                                auto it_c = kv.second.find(e16.cp);
                                if (it_c != kv.second.end()) {
                                    donor = it_c->second;
                                    break;
                                }
                            }
                        }
                        if (!donor) continue;

                        int src_dim = (donor->height >= 28) ? 32 : 24;
                        int std_h16 = fam16_std_h.count(e16.family) ? fam16_std_h[e16.family] : e16.height;
                        std::sort(e16.alt_hashes.begin(), e16.alt_hashes.end());

                        if (!first_g) jss << ",\n";
                        first_g = false;
                        char cbuf[32];
                        snprintf(cbuf, sizeof(cbuf), "U+%04X", e16.cp);
                        jss << "    {\n"
                            << "      \"index_16\": " << idx16 << ",\n"
                            << "      \"donor_cell_32\": " << donor->idx << ",\n"
                            << "      \"char_code\": \"" << cbuf << "\",\n"
                            << "      \"adv16\": " << e16.adv_x << ",\n"
                            << "      \"h16\": " << e16.height << ",\n"
                            << "      \"std_h16\": " << std_h16 << ",\n"
                            << "      \"src_dim\": " << src_dim << ",\n"
                            << "      \"tile_hash\": \"" << e16.tile_hash << "\",\n"
                            << "      \"exact_hash\": \"" << e16.exact_hash << "\",\n"
                            << "      \"family_hash\": \"" << e16.family << "\",\n"
                            << "      \"alt_hashes\": [";
                        for (size_t ai = 0; ai < e16.alt_hashes.size(); ++ai) {
                            if (ai > 0) jss << ", ";
                            jss << "\"" << e16.alt_hashes[ai] << "\"";
                        }
                        jss << "]\n    }";
                    }
                    jss << "\n  ]\n}\n";
                    WriteWholeFileText(out_fonts_dir / "MASTER_EN_font_32x32.json", jss.str());
                    res.cnt_font_json++;
                }
            }
        }
    }

    return res;
}

// ============================================================================
// 7. Full Archive Discovery, Stamp Verification & Automatic Conversion Pipeline
// ============================================================================

static void AutoInstallTexturePacksIfPresent(
    const fs::path& mods_dir,
    const fs::path& textures_dir,
    bool enabled,
    bool delete_source_archives)
{
    if (!enabled) return;

    std::error_code ec;
    const fs::path game_root = mods_dir.parent_path();
    std::vector<fs::path> search_dirs = {
        mods_dir,
        textures_dir,
        game_root
    };

    // 1. Discover OstrichMods source (actrilogy-hud-upgrade*.zip or SLUS-21346/replacements/hud-upgrade)
    fs::path ostrich_zip_path;
    fs::path ostrich_dir_path;
    for (const auto& d : search_dirs) {
        if (d.empty() || !fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
        for (const auto& entry : fs::directory_iterator(d, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string fn_low = ToLowerAscii(entry.path().filename().string());
            if (fn_low.rfind("actrilogy-hud-upgrade", 0) == 0 &&
                fn_low.size() > 4 && fn_low.substr(fn_low.size() - 4) == ".zip") {
                ostrich_zip_path = entry.path();
            }
        }
        if (!ostrich_zip_path.empty()) break;

        fs::path cand1 = d / "SLUS-21346" / "replacements" / "hud-upgrade";
        fs::path cand2 = d / "replacements" / "hud-upgrade";
        if (fs::is_directory(cand1, ec)) { ostrich_dir_path = cand1; break; }
        if (fs::is_directory(cand2, ec)) { ostrich_dir_path = cand2; break; }
    }

    // 2. Discover custom textures / texture converter archives and local For OstrichMods folder
    std::vector<fs::path> tc_zips;
    std::unordered_set<std::string> seen_zips;
    auto add_tc_zip = [&](const fs::path& p) {
        if (!fs::is_regular_file(p, ec)) return;
        std::string key = ToLowerAscii(fs::weakly_canonical(p, ec).string());
        if (seen_zips.insert(key).second) {
            tc_zips.push_back(p);
        }
    };

    for (const auto& d : search_dirs) {
        if (d.empty() || !fs::exists(d, ec) || !fs::is_directory(d, ec)) continue;
        for (const char* exact : {"custom textures.zip", "custom_textures.zip", "texture converter.zip", "texture_converter.zip", "mods.zip"}) {
            add_tc_zip(d / exact);
        }
        for (const auto& entry : fs::directory_iterator(d, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string fn_low = ToLowerAscii(entry.path().filename().string());
            if (fn_low.size() <= 4 || fn_low.substr(fn_low.size() - 4) != ".zip") continue;
            if (fn_low.rfind("custom textures", 0) == 0 ||
                fn_low.rfind("custom_textures", 0) == 0 ||
                fn_low.rfind("texture converter", 0) == 0 ||
                fn_low.rfind("texture_converter", 0) == 0) {
                add_tc_zip(entry.path());
            }
        }
    }

    fs::path fom_dir;
    for (const auto& d : search_dirs) {
        if (d.empty()) continue;
        fs::path cand = d / "For OstrichMods";
        if (fs::is_directory(cand, ec)) {
            fom_dir = cand;
            break;
        }
    }

    if (ostrich_zip_path.empty() && ostrich_dir_path.empty() && tc_zips.empty() && fom_dir.empty()) {
        return; // Nothing to auto-install
    }

    // Build deterministic stamp signature of all discovered sources
    auto file_sig = [&](const fs::path& p) -> std::string {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &fad)) return p.string();
        uint64_t sz = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        uint64_t mt = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
        return p.string() + "|" + std::to_string(sz) + "|" + std::to_string(mt);
    };

    std::ostringstream stamp_ss;
    stamp_ss << "ACZ_NATIVE_INSTALLER_V2\n";
    if (!ostrich_zip_path.empty()) stamp_ss << "OSTRICH_ZIP:" << file_sig(ostrich_zip_path) << "\n";
    if (!ostrich_dir_path.empty()) stamp_ss << "OSTRICH_DIR:" << ostrich_dir_path.string() << "\n";
    for (const auto& zp : tc_zips) {
        stamp_ss << "TC_ZIP:" << file_sig(zp) << "\n";
    }
    if (!fom_dir.empty()) stamp_ss << "FOM_DIR:" << fom_dir.string() << "\n";
    std::string current_stamp = stamp_ss.str();

    const fs::path output_dir = textures_dir / "ostrichmods";
    const fs::path stamp_path = output_dir / ".install_stamp";
    const fs::path probe_air  = output_dir / "icons-aircraft" / "f15c_f15e_7BBFF633CB7C5D32.png";

    if (!delete_source_archives) {
        std::vector<uint8_t> old_stamp_bytes;
        if (ReadWholeFileBytes(stamp_path, old_stamp_bytes)) {
            std::string old_stamp(reinterpret_cast<const char*>(old_stamp_bytes.data()), old_stamp_bytes.size());
            bool has_ostrich = (!ostrich_zip_path.empty() || !ostrich_dir_path.empty());
            if (old_stamp == current_stamp && (!has_ostrich || fs::exists(probe_air, ec))) {
                return; // Already installed and up-to-date!
            }
        }
    }

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    LogMsg("[ACZ-AutoInstaller] Detected texture pack archive(s) in mods folder. Running built-in C++ installer...\n");

    // ------------------------------------------------------------------------
    // Step A: Install direct textures from `textures/` inside tc_zips (and local staging)
    // ------------------------------------------------------------------------
    int cnt_direct_tex = 0;
    for (const auto& zp : tc_zips) {
        ZipArchiveReader zf;
        if (!zf.open(zp)) continue;
        for (const auto& e : zf.entries) {
            if (e.is_dir) continue;
            std::string norm_p = e.filename;
            while (!norm_p.empty() && norm_p.front() == '/') norm_p.erase(norm_p.begin());

            // Split path components
            std::vector<std::string> parts;
            size_t pos = 0;
            while (pos < norm_p.size()) {
                size_t slash = norm_p.find('/', pos);
                if (slash == std::string::npos) {
                    parts.push_back(norm_p.substr(pos));
                    break;
                }
                parts.push_back(norm_p.substr(pos, slash - pos));
                pos = slash + 1;
            }
            if (parts.size() < 2) continue;

            int tex_idx = -1;
            for (size_t idx = 0; idx + 1 < parts.size(); ++idx) {
                if (ToLowerAscii(parts[idx]) == "textures" &&
                    (idx == 0 || ToLowerAscii(parts[idx - 1]) != "mods")) {
                    tex_idx = static_cast<int>(idx);
                    break;
                }
            }
            if (tex_idx != -1) {
                fs::path rel_sub;
                for (size_t k = static_cast<size_t>(tex_idx + 1); k < parts.size(); ++k) {
                    rel_sub /= parts[k];
                }
                std::vector<uint8_t> data;
                if (zf.extract_entry(e, data)) {
                    if (WriteWholeFileBytes(textures_dir / rel_sub, data.data(), data.size())) {
                        cnt_direct_tex++;
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Step B: Load source files (hud_files, spw_files, master_font_files)
    // ------------------------------------------------------------------------
    std::unordered_map<std::string, std::vector<uint8_t>> hud_files;
    std::unordered_map<std::string, std::vector<uint8_t>> spw_files;
    std::unordered_map<std::string, std::vector<uint8_t>> master_font_files;

    const std::string kAczSubpathPrefix = "SLUS-21346/replacements/hud-upgrade/";
    if (!ostrich_zip_path.empty()) {
        ZipArchiveReader zf;
        if (zf.open(ostrich_zip_path)) {
            for (const auto& e : zf.entries) {
                if (e.is_dir) continue;
                std::string fn_low = ToLowerAscii(e.filename);
                if (fn_low.size() <= 4 || fn_low.substr(fn_low.size() - 4) != ".png") continue;
                size_t idx = e.filename.find(kAczSubpathPrefix);
                if (idx != std::string::npos) {
                    std::string rel = e.filename.substr(idx + kAczSubpathPrefix.size());
                    std::vector<uint8_t> data;
                    if (zf.extract_entry(e, data)) {
                        hud_files[rel] = std::move(data);
                    }
                }
            }
        }
    } else if (!ostrich_dir_path.empty()) {
        for (const auto& entry : fs::recursive_directory_iterator(ostrich_dir_path, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = ToLowerAscii(entry.path().extension().string());
            if (ext != ".png") continue;
            std::string rel = fs::relative(entry.path(), ostrich_dir_path, ec).generic_string();
            std::vector<uint8_t> data;
            if (ReadWholeFileBytes(entry.path(), data)) {
                hud_files[rel] = std::move(data);
            }
        }
    }

    const std::unordered_set<std::string> stray_slus20851 = {
        "4ea2bcf12b88b332-5b222812b8aad085-00001dd3.png",
        "ddf0c0d7a75144c9-25f11bc8fcbc8d46-00001dd3.png"
    };

    for (const auto& zp : tc_zips) {
        ZipArchiveReader ztc;
        if (!ztc.open(zp)) continue;
        for (const auto& e : ztc.entries) {
            if (e.is_dir) continue;
            std::string norm_fn = e.filename;
            std::string low_fn = ToLowerAscii(norm_fn);
            if (low_fn.find("textures/") != std::string::npos &&
                low_fn.find("for ostrichmods/") == std::string::npos) {
                continue;
            }
            size_t slash = norm_fn.rfind('/');
            std::string bname = (slash == std::string::npos) ? norm_fn : norm_fn.substr(slash + 1);
            std::string bname_low = ToLowerAscii(bname);
            if (bname.empty() || stray_slus20851.count(bname_low)) continue;

            if (bname_low.rfind("master_", 0) == 0 &&
                (bname_low.size() > 4 && (bname_low.substr(bname_low.size() - 4) == ".png" ||
                                          bname_low.substr(bname_low.size() - 4) == ".txt"))) {
                if (master_font_files.find(bname) == master_font_files.end()) {
                    std::vector<uint8_t> data;
                    if (ztc.extract_entry(e, data)) master_font_files[bname] = std::move(data);
                }
            } else if (bname_low.size() > 4 && bname_low.substr(bname_low.size() - 4) == ".png") {
                if (spw_files.find(bname_low) == spw_files.end()) {
                    std::vector<uint8_t> data;
                    if (ztc.extract_entry(e, data)) spw_files[bname_low] = std::move(data);
                }
            }
        }
    }

    // Also scan local For OstrichMods / spw_dir / search_dirs for MASTER_EN_font_* and SPW pngs
    const fs::path out_spw_dir = output_dir / "icons-weapons" / "spw";
    std::vector<fs::path> check_dirs;
    if (!fom_dir.empty()) check_dirs.push_back(fom_dir);
    if (fs::is_directory(out_spw_dir, ec)) check_dirs.push_back(out_spw_dir);
    for (const auto& d : search_dirs) {
        if (!d.empty() && fs::is_directory(d, ec)) check_dirs.push_back(d);
    }
    for (const auto& cdir : check_dirs) {
        bool is_spw_or_fom = (cdir == out_spw_dir || cdir == fom_dir);
        for (const auto& entry : fs::directory_iterator(cdir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string fn = entry.path().filename().string();
            std::string bname_low = ToLowerAscii(fn);
            if (stray_slus20851.count(bname_low)) continue;
            if (bname_low.rfind("master_", 0) == 0 &&
                bname_low.size() > 4 &&
                (bname_low.substr(bname_low.size() - 4) == ".png" ||
                 bname_low.substr(bname_low.size() - 4) == ".txt")) {
                if (master_font_files.find(fn) == master_font_files.end()) {
                    std::vector<uint8_t> data;
                    if (ReadWholeFileBytes(entry.path(), data)) master_font_files[fn] = std::move(data);
                }
            } else if (is_spw_or_fom && bname_low.size() > 4 && bname_low.substr(bname_low.size() - 4) == ".png") {
                if (spw_files.find(bname_low) == spw_files.end()) {
                    std::vector<uint8_t> data;
                    if (ReadWholeFileBytes(entry.path(), data)) spw_files[bname_low] = std::move(data);
                }
            }
        }
    }

    if (hud_files.empty() && spw_files.empty() && master_font_files.empty() && cnt_direct_tex == 0) {
        return;
    }

    const fs::path out_air_dir         = output_dir / "icons-aircraft";
    const fs::path out_weap_dir        = output_dir / "icons-weapons";
    const fs::path out_atl_dir         = out_weap_dir / "atlases";
    const fs::path out_diag_dir        = out_weap_dir / "descriptions";
    const fs::path out_hangar_weap_dir = out_weap_dir / "hangar-weapons";
    const fs::path out_ux_dir          = output_dir / "ux";
    const fs::path out_emb_dir         = output_dir / "ux" / "intro-squadron" / "emblems";
    const fs::path out_fonts_dir       = output_dir / "ux" / "intro-squadron" / "fonts";

    fs::create_directories(out_air_dir, ec);
    fs::create_directories(out_weap_dir, ec);
    fs::create_directories(out_ux_dir, ec);
    fs::create_directories(out_emb_dir, ec);

    // Build lookup maps from static tables
    std::unordered_map<std::string, std::string> aircraft_map;
    for (const auto& p : kAircraftMap) aircraft_map[p.k] = p.v;

    std::unordered_map<std::string, const WeapMapEntry*> weapons_map;
    for (const auto& w : kWeaponsMap) weapons_map[w.src_fn] = &w;

    std::unordered_map<std::string, std::string> ux_specs;
    for (const auto& u : kUxSpecs) ux_specs[u.k] = u.v;

    std::unordered_map<std::string, const EmblemSpec*> emblems_map;
    for (const auto& em : kEmblemsMap) emblems_map[em.src_fn] = &em;

    int cnt_air = 0, cnt_weap = 0, cnt_ux = 0, cnt_emb = 0;
    int cnt_atl = 0, cnt_diag = 0, cnt_hangar_weap = 0, cnt_json = 0;

    std::unordered_map<std::string, TexImgRgba> processed_weap_imgs;
    std::unordered_map<std::string, std::vector<uint8_t>> intro_letterhead;
    std::unordered_map<std::string, std::vector<uint8_t>> intro_upper;
    std::unordered_map<std::string, std::vector<uint8_t>> intro_lower;

    // ------------------------------------------------------------------------
    // Step C: Convert OstrichMods HUD files (aircraft, weapons, ux, emblems, intro glyphs)
    // ------------------------------------------------------------------------
    for (const auto& kv : hud_files) {
        const std::string& rel_path = kv.first;
        const std::vector<uint8_t>& png_bytes = kv.second;

        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < rel_path.size()) {
            size_t slash = rel_path.find('/', pos);
            if (slash == std::string::npos) {
                parts.push_back(rel_path.substr(pos));
                break;
            }
            parts.push_back(rel_path.substr(pos, slash - pos));
            pos = slash + 1;
        }
        if (parts.empty()) continue;

        std::string folder = ToLowerAscii(parts[0]);
        const std::string& filename = parts.back();

        if (folder == "icons-aircraft" && parts.size() == 2) {
            if (filename == "e6d874b58048e40c-d6599b7ebe3fba5d-00001993.png") continue;
            auto it = aircraft_map.find(filename);
            if (it != aircraft_map.end()) {
                TexImgRgba src_im;
                if (DecodePngBytesViaWic(png_bytes.data(), png_bytes.size(), src_im)) {
                    TexImgRgba out_im = ProcessAircraftIcon(src_im);
                    if (SavePngFileViaWic(out_air_dir / it->second, out_im)) cnt_air++;
                }
            }
        } else if (folder == "icons-weapons" && parts.size() == 2) {
            auto it = weapons_map.find(filename);
            if (it != weapons_map.end()) {
                TexImgRgba src_im;
                if (DecodePngBytesViaWic(png_bytes.data(), png_bytes.size(), src_im)) {
                    TexImgRgba out_im = ProcessWeaponIcon(src_im);
                    processed_weap_imgs[ToLowerAscii(filename)] = out_im;
                    const WeapMapEntry* we = it->second;
                    if (!we->out2 || we->out2[0] == '\0') {
                        if (SavePngFileViaWic(out_weap_dir / we->out1, out_im)) cnt_weap++;
                    } else {
                        auto extract_hash = [](const std::string& tn) -> std::string {
                            size_t us = tn.rfind('_');
                            size_t dot = tn.rfind('.');
                            if (us != std::string::npos && dot != std::string::npos && dot > us + 1) {
                                return tn.substr(us + 1, dot - (us + 1));
                            }
                            return "";
                        };
                        std::string h1 = extract_hash(we->out1);
                        std::string h2 = extract_hash(we->out2);
                        std::string shared_name = "typn_rm01_spw_xlaa_" + h1 + ".png";
                        fs::path out_p = out_weap_dir / shared_name;
                        if (SavePngFileViaWic(out_p, out_im)) {
                            WriteCompanionHashJson(
                                out_p,
                                "Shared XLAA weapon icon (Typhoon spw2 & Rafale M spw3)",
                                0, 0,
                                {h1, h2});
                            cnt_weap++;
                            cnt_json++;
                        }
                    }
                }
            }
        } else if (folder == "ux") {
            if (parts.size() == 2) {
                auto it = ux_specs.find(filename);
                if (it != ux_specs.end()) {
                    TexImgRgba src_im;
                    if (DecodePngBytesViaWic(png_bytes.data(), png_bytes.size(), src_im)) {
                        TexImgRgba out_im = ProcessUxAtlas(src_im);
                        if (SavePngFileViaWic(out_ux_dir / it->second, out_im)) cnt_ux++;
                    }
                }
            } else if (parts.size() >= 3 && ToLowerAscii(parts[1]) == "intro-squadron") {
                std::string sub = ToLowerAscii(parts[2]);
                if (sub == "emblems" && parts.size() == 4) {
                    auto it = emblems_map.find(filename);
                    if (it != emblems_map.end()) {
                        TexImgRgba src_im;
                        if (DecodePngBytesViaWic(png_bytes.data(), png_bytes.size(), src_im)) {
                            TexImgRgba out_im = ProcessSquadronEmblem(src_im, *it->second, true);
                            if (SavePngFileViaWic(out_emb_dir / it->second->out_fn, out_im)) cnt_emb++;
                        }
                    }
                } else if (sub == "letterhead" && parts.size() == 4) {
                    intro_letterhead[ToLowerAscii(filename)] = png_bytes;
                } else if (sub == "upper-num" && parts.size() == 4) {
                    intro_upper[ToLowerAscii(filename)] = png_bytes;
                } else if (sub == "lower" && parts.size() == 4) {
                    intro_lower[ToLowerAscii(filename)] = png_bytes;
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Step D: Build HD Master Font Atlases (64x64, 32x32 + 16x16 JSON + Subtitle Brackets)
    // ------------------------------------------------------------------------
    MasterFontBuildResult font_res = BuildMasterFontAtlases(
        master_font_files,
        intro_letterhead,
        intro_upper,
        intro_lower,
        out_fonts_dir);
    cnt_json += font_res.cnt_font_json;

    // ------------------------------------------------------------------------
    // Step E: Process UGBS Icon (3b97db2f5eb61035.png)
    // ------------------------------------------------------------------------
    bool ugbs_found = false;
    for (const char* cand_key : {"3b97db2f5eb61035.png", "f01a_spw2_3b97db2f5eb61035.png", "ugbs.png"}) {
        auto it_u = spw_files.find(cand_key);
        if (it_u != spw_files.end()) {
            TexImgRgba raw_ugbs;
            if (DecodePngBytesViaWic(it_u->second.data(), it_u->second.size(), raw_ugbs)) {
                TexImgRgba ugbs_hd = ProcessCustomUgbsIcon(raw_ugbs);
                if (!ugbs_hd.empty()) {
                    SavePngFileViaWic(out_weap_dir / "f01a_f05e_j35j_spw2_3B97DB2F5EB61035.png", ugbs_hd);
                    processed_weap_imgs["3b97db2f5eb61035.png"] = std::move(ugbs_hd);
                    cnt_weap++;
                    ugbs_found = true;
                    break;
                }
            }
        }
    }
    if (!ugbs_found) {
        std::vector<uint8_t> fb_raw = DecodeBase64(kFallbackUgbs1xB64);
        TexImgRgba fb_im;
        if (DecodePngBytesViaWic(fb_raw.data(), fb_raw.size(), fb_im)) {
            processed_weap_imgs["3b97db2f5eb61035.png"] = ResizePremultipliedLanczos(fb_im, 256, 256);
        }
    }

    // ------------------------------------------------------------------------
    // Step F: Assemble Hangar SPW Atlases (36), TIM2 Weapon Icons (60), & Diagrams (19)
    // ------------------------------------------------------------------------
    bool has_ac5_descs = false;
    for (const auto& kv : spw_files) {
        if (kv.first.find("00001dd3") != std::string::npos) {
            has_ac5_descs = true;
            break;
        }
    }

    if (has_ac5_descs) {
        fs::create_directories(out_atl_dir, ec);
        fs::create_directories(out_diag_dir, ec);
        fs::create_directories(out_hangar_weap_dir, ec);

        std::unordered_map<int, TexImgRgba> processed_desc_clusters;
        std::unordered_map<int, const DescClusterSpec*> desc_spec_by_cid;
        std::vector<uint8_t> fb_mpbm_raw = DecodeBase64(kFallbackMpbmDesc1xB64);
        TexImgRgba fb_mpbm_im;
        DecodePngBytesViaWic(fb_mpbm_raw.data(), fb_mpbm_raw.size(), fb_mpbm_im);

        for (const auto& dc : kDescClustersMap) {
            desc_spec_by_cid[dc.cid] = &dc;
            std::string key_low = ToLowerAscii(dc.desc_fn);
            auto it_spw = spw_files.find(key_low);
            if (it_spw != spw_files.end()) {
                TexImgRgba spw_src;
                if (DecodePngBytesViaWic(it_spw->second.data(), it_spw->second.size(), spw_src)) {
                    processed_desc_clusters[dc.cid] = ProcessAc5DescriptionDiagram(spw_src, dc);
                }
            } else if (!fb_mpbm_im.empty()) {
                TexImgRgba crop_1x = CropImage(fb_mpbm_im, dc.dx0 / 8, dc.dy0 / 8, dc.dx1 / 8, dc.dy1 / 8);
                processed_desc_clusters[dc.cid] = ResizePremultipliedLanczos(crop_1x, dc.dx1 - dc.dx0, dc.dy1 - dc.dy0);
            }
        }

        std::unordered_map<std::string, TexImgRgba> boosted_weap_imgs;
        for (const auto& kv : processed_weap_imgs) {
            boosted_weap_imgs[kv.first] = BoostWeaponIconForHangarAtlas(kv.second);
        }

        // F1. 36 Composite Hangar SPW Atlases (2048x1024) -> icons-weapons\atlases
        for (const auto& aspec : kHangarAtlasMap) {
            TexImgRgba canvas(2048, 1024);
            for (int slot_i = 0; slot_i < 3; ++slot_i) {
                const auto& slot = aspec.slots[slot_i];
                auto it_w = boosted_weap_imgs.find(ToLowerAscii(slot.weap_fn));
                if (it_w != boosted_weap_imgs.end() && !it_w->second.empty()) {
                    PasteImage(canvas, it_w->second, slot_i * 320 + 32, 32);
                }
                auto it_d = processed_desc_clusters.find(slot.desc_cid);
                auto it_ds = desc_spec_by_cid.find(slot.desc_cid);
                if (it_d != processed_desc_clusters.end() && it_ds != desc_spec_by_cid.end() && !it_d->second.empty()) {
                    PasteImage(canvas, it_d->second, slot_i * 640 + it_ds->second->dx0, 512 + it_ds->second->dy0);
                }
            }
            if (SavePngFileViaWic(out_atl_dir / aspec.atlas_name, canvas)) {
                cnt_atl++;
            }
        }

        // F2. 60 Hangar TIM2 320x320 Special Weapon Icons (+ .json) -> icons-weapons\hangar-weapons
        for (const auto& wspec : kHangarTim2WeapSpecs) {
            auto it_w = processed_weap_imgs.find(ToLowerAscii(wspec.weap_fn));
            if (it_w == processed_weap_imgs.end() || it_w->second.empty()) continue;
            const TexImgRgba& base_im = it_w->second;

            int bx0 = base_im.width, by0 = base_im.height, bx1 = -1, by1 = -1;
            for (int y = 0; y < base_im.height; ++y) {
                for (int x = 0; x < base_im.width; ++x) {
                    if (base_im.pixel_ptr(x, y)[3] > 8) {
                        if (x < bx0) bx0 = x;
                        if (y < by0) by0 = y;
                        if (x > bx1) bx1 = x;
                        if (y > by1) by1 = y;
                    }
                }
            }
            if (bx1 < bx0) {
                bx0 = 0; by0 = 0; bx1 = base_im.width - 1; by1 = base_im.height - 1;
            }
            int cw = bx1 - bx0 + 1;
            int ch = by1 - by0 + 1;
            std::vector<uint8_t> crop_a(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0);
            for (int y = 0; y < ch; ++y) {
                for (int x = 0; x < cw; ++x) {
                    crop_a[static_cast<size_t>(y) * cw + x] = base_im.pixel_ptr(bx0 + x, by0 + y)[3];
                }
            }

            int tw = std::max(1, wspec.dx1 - wspec.dx0);
            int th = std::max(1, wspec.dy1 - wspec.dy0);
            std::vector<float> res_a;
            ResizeLanczosGrayFloat(crop_a.data(), cw, ch, res_a, tw, th);

            TexImgRgba tile_320(320, 320);
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    float a = res_a[static_cast<size_t>(y) * tw + x];
                    float a_tim2 = (a <= 62.0f)
                        ? (a * (108.0f / 62.0f))
                        : (108.0f + (a - 62.0f) * (255.0f - 108.0f) / (255.0f - 62.0f));
                    uint8_t av = static_cast<uint8_t>(std::clamp(std::round(a_tim2), 0.0f, 255.0f));
                    if (av > 0 && wspec.dx0 + x >= 0 && wspec.dx0 + x < 320 && wspec.dy0 + y >= 0 && wspec.dy0 + y < 320) {
                        uint8_t* dp = tile_320.pixel_ptr(wspec.dx0 + x, wspec.dy0 + y);
                        dp[0] = 254; dp[1] = 254; dp[2] = 254; dp[3] = av;
                    }
                }
            }
            fs::path out_p = out_hangar_weap_dir / wspec.out_name;
            if (SavePngFileViaWic(out_p, tile_320)) {
                std::vector<std::string> hlist;
                for (int k = 0; k < 3; ++k) {
                    if (wspec.hashes[k] && wspec.hashes[k][0] != '\0') hlist.push_back(wspec.hashes[k]);
                }
                WriteCompanionHashJson(
                    out_p,
                    std::string("Hangar TIM2 Special Weapon Icon (") + wspec.out_name + ")",
                    40, 40,
                    hlist);
                cnt_hangar_weap++;
                cnt_json++;
            }
        }

        // F3. 19 Hangar TIM2 512x512 [STATISTICS] Description Diagrams (+ .json) -> icons-weapons\descriptions
        for (const auto& dspec : kHangarTim2DiagSpecs) {
            auto it_ds = desc_spec_by_cid.find(dspec.cid);
            if (it_ds == desc_spec_by_cid.end()) continue;
            const DescClusterSpec& dc = *it_ds->second;
            std::string key_low = ToLowerAscii(dc.desc_fn);

            int tw = std::max(1, dspec.dx1 - dspec.dx0);
            int th = std::max(1, dspec.dy1 - dspec.dy0);
            std::vector<uint8_t> a_u8(static_cast<size_t>(tw) * static_cast<size_t>(th), 0);
            bool ok_diag = false;

            auto it_spw = spw_files.find(key_low);
            if (it_spw != spw_files.end()) {
                TexImgRgba im_spw;
                if (DecodePngBytesViaWic(it_spw->second.data(), it_spw->second.size(), im_spw)) {
                    const size_t n = static_cast<size_t>(im_spw.width) * static_cast<size_t>(im_spw.height);
                    std::vector<float> a_spw(n, 0.0f);
                    std::vector<float> nz;
                    for (size_t i = 0; i < n; ++i) {
                        float af = static_cast<float>(im_spw.px[i * 4u + 3]);
                        a_spw[i] = af;
                        if (af > 0.0f) nz.push_back(af);
                    }
                    if (!nz.empty()) {
                        float p80 = ComputePercentile(nz, 80.0f);
                        if (p80 > 0.0f) {
                            for (size_t i = 0; i < n; ++i) {
                                a_spw[i] = std::min(255.0f, a_spw[i] * (255.0f / p80) * 1.15f);
                            }
                        }
                    }
                    int sx0 = (im_spw.width == 512 && im_spw.height == 512) ? dc.sx0 : 0;
                    int sy0 = (im_spw.width == 512 && im_spw.height == 512) ? dc.sy0 : 0;
                    int sx1 = (im_spw.width == 512 && im_spw.height == 512) ? dc.sx1 : im_spw.width;
                    int sy1 = (im_spw.width == 512 && im_spw.height == 512) ? dc.sy1 : im_spw.height;
                    int cw = std::max(1, sx1 - sx0);
                    int ch = std::max(1, sy1 - sy0);
                    std::vector<uint8_t> cropped_a(static_cast<size_t>(cw) * static_cast<size_t>(ch), 0);
                    for (int y = 0; y < ch; ++y) {
                        for (int x = 0; x < cw; ++x) {
                            float v = a_spw[static_cast<size_t>(sy0 + y) * im_spw.width + (sx0 + x)];
                            cropped_a[static_cast<size_t>(y) * cw + x] = static_cast<uint8_t>(std::clamp(std::round(v), 0.0f, 255.0f));
                        }
                    }
                    std::vector<float> res_a;
                    ResizeLanczosGrayFloat(cropped_a.data(), cw, ch, res_a, tw, th);
                    for (size_t i = 0; i < res_a.size(); ++i) {
                        a_u8[i] = static_cast<uint8_t>(std::clamp(std::round(std::min(255.0f, res_a[i] * 1.25f)), 0.0f, 255.0f));
                    }
                    ok_diag = true;
                }
            } else {
                auto it_dim = processed_desc_clusters.find(dspec.cid);
                if (it_dim != processed_desc_clusters.end() && !it_dim->second.empty()) {
                    TexImgRgba resized_dim = ResizePremultipliedLanczos(it_dim->second, tw, th);
                    for (size_t i = 0; i < a_u8.size(); ++i) {
                        a_u8[i] = resized_dim.px[i * 4u + 3];
                    }
                    ok_diag = true;
                }
            }

            if (ok_diag) {
                TexImgRgba tile_512(512, 512);
                for (int y = 0; y < th; ++y) {
                    for (int x = 0; x < tw; ++x) {
                        uint8_t av = a_u8[static_cast<size_t>(y) * tw + x];
                        if (av > 0 && dspec.dx0 + x >= 0 && dspec.dx0 + x < 512 && dspec.dy0 + y >= 0 && dspec.dy0 + y < 512) {
                            uint8_t* dp = tile_512.pixel_ptr(dspec.dx0 + x, dspec.dy0 + y);
                            dp[0] = 254; dp[1] = 254; dp[2] = 254; dp[3] = av;
                        }
                    }
                }
                fs::path out_p = out_diag_dir / dspec.out_name;
                if (SavePngFileViaWic(out_p, tile_512)) {
                    std::vector<std::string> hlist;
                    for (int k = 0; k < 3; ++k) {
                        if (dspec.hashes[k] && dspec.hashes[k][0] != '\0') hlist.push_back(dspec.hashes[k]);
                    }
                    char cmt[128];
                    snprintf(cmt, sizeof(cmt), "Hangar TIM2 [STATISTICS] Weapon Trajectory Diagram (Cluster #%02d)", dspec.cid);
                    WriteCompanionHashJson(out_p, cmt, 64, 64, hlist);
                    cnt_diag++;
                    cnt_json++;
                }
            }
        }
    }

    int total_written = cnt_air + cnt_weap + cnt_ux + cnt_emb + font_res.cnt_atlases +
                        cnt_atl + cnt_diag + cnt_hangar_weap + cnt_direct_tex;

    if (total_written > 0) {
        WriteWholeFileText(stamp_path, current_stamp);
        if (delete_source_archives) {
            if (!ostrich_zip_path.empty()) fs::remove(ostrich_zip_path, ec);
            for (const auto& zp : tc_zips) fs::remove(zp, ec);
        }
    }

    QueryPerformanceCounter(&t1);
    double elapsed_ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    LogMsg("[ACZ-AutoInstaller] Completed in %.1f ms: Direct=%d, Air=%d, Weap=%d, UX=%d, Emb=%d, Fonts=%d (%d glyphs), HangarAtl=%d, HangarWeap=%d, HangarDiag=%d, JSON=%d.\n",
           elapsed_ms, cnt_direct_tex, cnt_air, cnt_weap, cnt_ux, cnt_emb,
           font_res.cnt_atlases, font_res.cnt_glyphs, cnt_atl, cnt_hangar_weap, cnt_diag, cnt_json);
}
#endif

} // namespace acz_installer
