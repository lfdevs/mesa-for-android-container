// Copyright (c) 2022-2024 Advanced Micro Devices, Inc. All rights reserved.

#include <string.h>
#include <math.h>
#include "color_bg.h"
#include "vpe_priv.h"

struct csc_vector {
    float x;
    float y;
    float z;
};

struct csc_table {
    struct csc_vector rgb_offset; // RGB offset
    struct csc_vector red_coef;   // RED coefficient
    struct csc_vector green_coef; // GREEN coefficient
    struct csc_vector blue_coef;  // BLUE coefficient
};


const double bt_709_rgb_xyz_matrix[] = {
    0.135676572958501,   0.117645247657296,   0.059378179384203,
    0.069958232931727,   0.235290495314592,   0.023751271753681,
    0.006359839357430,   0.039215082552432,   0.312725078090138
};

const double bt_601_rgb_xyz_matrix[] = {
    0.129468377303939,   0.120169907240092,   0.063061715455969,
    0.069871822671967,   0.230648692928563,   0.028479484399470,
    0.006165160823997,   0.036826261896157,   0.315308577279846
};

const double bt_2020_rgb_xyz_matrix[] = {
    0.209559197891125,   0.047578961279863,   0.055561840829013,
    0.086428369751707,   0.223061365529709,   0.019510264718585,
    0.000000000000000,   0.009235916013150,   0.349064083986850
};

const double bt_709_xyz_rgb_matrix[] = {
    9.850972467794900,    -4.672897196261683,    -1.515534225814599,
   -2.946029289607537,     5.702028879962675,     0.126307165371354,
    0.169088388136759,    -0.619990756501448,     3.212679374598414
};

const double bt_601_xyz_rgb_matrix[] = {
    10.656544932293809,   -5.288117709127149,    -1.653672548215019,
   -3.249384680406732,     6.011485965740993,     0.106904010143450,
    0.171144655726832,    -0.598710197023623,     3.191344462670923
};

const double bt_2020_xyz_rgb_matrix[] = {
    5.217784765870115,    -1.081066212086299,    -0.770110277731489,
   -2.026396206177778,     4.913316828677627,     0.047928710680581,
    0.053616587979668,    -0.130001864005497,     2.863535322904176
};

static struct csc_table bgcolor_to_rgbfull_table[COLOR_SPACE_MAX] = {
    [COLOR_SPACE_YCBCR601] =
        {
            {0.0f, -0.5f, -0.5f},
            {1.0f, 0.0f, 1.402f},
            {1.0f, -0.344136286f, -0.714136286f},
            {1.0f, 1.772f, 0.0f},
        },
    [COLOR_SPACE_YCBCR709] =
        {
            {0.0f, -0.5f, -0.5f},
            {1.0f, 0.0f, 1.5748f},
            {1.0f, -0.187324273f, -0.468124273f},
            {1.0f, 1.8556f, 0.0f},
        },
    [COLOR_SPACE_YCBCR601_LIMITED] =
        {
            {-0.0625f, -0.5f, -0.5f},
            {1.164383562f, 0.0f, 1.596026786f},
            {1.164383562f, -0.39176229f, -0.812967647f},
            {1.164383562f, 2.017232143f, 0.0f},
        },
    [COLOR_SPACE_YCBCR709_LIMITED] =
        {
            {-0.0625f, -0.5f, -0.5f},
            {1.164383562f, 0.0f, 1.792741071f},
            {1.164383562f, -0.213248614f, -0.532909329f},
            {1.164383562f, 2.112401786f, 0.0f},
        },
    [COLOR_SPACE_2020_YCBCR] =
        {
            {0.0f, -512.f / 1023.f, -512.f / 1023.f},
            {1.0f, 0.0f, 1.4746f},
            {1.0f, -0.164553127f, -0.571353127f},
            {1.0f, 1.8814f, 0.0f},
        },
    [COLOR_SPACE_2020_YCBCR_LIMITED] =
        {
            {-0.0625f, -0.5f, -0.5f},
            {1.167808219f, 0.0f, 1.683611384f},
            {1.167808219f, -0.187877063f, -0.652337331f},
            {1.167808219f, 2.148071652f, 0.0f},
        },
    [COLOR_SPACE_SRGB_LIMITED] =
        {
            {-0.0626221f, -0.0626221f, -0.0626221f},
            {1.167783652f, 0.0f, 0.0f},
            {0.0f, 1.167783652f, 0.0f},
            {0.0f, 0.0, 1.167783652f},
        },
    [COLOR_SPACE_2020_RGB_LIMITEDRANGE] = {
        {-0.0626221f, -0.0626221f, -0.0626221f},
        {1.167783652f, 0.0f, 0.0f},
        {0.0f, 1.167783652f, 0.0f},
        {0.0f, 0.0, 1.167783652f},
    }};

static double clip_double(double x)
{
    if (x < 0.0)
        return 0.0;
    else if (x > 1.0)
        return 1.0;
    else
        return x;
}

static float clip_float(float x)
{
    if (x < 0.0f)
        return 0.0f;
    else if (x > 1.0f)
        return 1.0f;
    else
        return x;
}

static void color_multiply_matrices_double(double *mResult, double *M1,
    double *M2, unsigned int Rows1, unsigned int Cols1, unsigned int Cols2)
{
    unsigned int i, j, k;

    for (i = 0; i < Rows1; i++) {
        for (j = 0; j < Cols2; j++) {
            mResult[(i * Cols2) + j] = 0.0;
            for (k = 0; k < Cols1; k++)
                mResult[(i * Cols2) + j] = mResult[(i * Cols2) + j] +
                    M1[(i * Cols1) + k] * M2[(k * Cols2) + j];
        }
    }
}

static void set_gamut_remap_matrix(double* res, enum color_space src_cs, enum color_space dst_cs) {

    double rgb_to_xyz[9] = { 0.0 };
    double xyz_to_rgb[9] = { 0.0 };

    switch (src_cs)
    {
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_MSREF_SCRGB:
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR_JFIF:
    case COLOR_SPACE_RGB_JFIF:
        memcpy(rgb_to_xyz, bt_709_rgb_xyz_matrix, 9 * sizeof(double));
        break;
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR601_LIMITED:
        memcpy(rgb_to_xyz, bt_601_rgb_xyz_matrix, 9 * sizeof(double));
        break;
    case COLOR_SPACE_2020_RGB_FULLRANGE:
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        memcpy(rgb_to_xyz, bt_2020_rgb_xyz_matrix, 9 * sizeof(double));
        break;
    default:
        VPE_ASSERT(0);
        break;
    }

    switch (dst_cs)
    {
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_MSREF_SCRGB:
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR_JFIF:
    case COLOR_SPACE_RGB_JFIF:
        memcpy(xyz_to_rgb, bt_709_xyz_rgb_matrix, 9 * sizeof(double));
        break;
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR601_LIMITED:
        memcpy(xyz_to_rgb, bt_601_xyz_rgb_matrix, 9 * sizeof(double));
        break;
    case COLOR_SPACE_2020_RGB_FULLRANGE:
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        memcpy(xyz_to_rgb, bt_2020_xyz_rgb_matrix, 9 * sizeof(double));
        break;
    default:
        VPE_ASSERT(0);
        break;
    }

    color_multiply_matrices_double(res, xyz_to_rgb, rgb_to_xyz, 3, 3, 3);

}

bool vpe_bg_csc(struct vpe_color *bg_color, enum color_space cs)
{
    struct csc_table *entry             = &bgcolor_to_rgbfull_table[cs];
    float             csc_final[3]      = {0};
    float             csc_mm[3][4]      = {0};
    bool              output_is_clipped = false;

    memcpy(&csc_mm[0][0], &entry->red_coef, sizeof(struct csc_vector));
    memcpy(&csc_mm[1][0], &entry->green_coef, sizeof(struct csc_vector));
    memcpy(&csc_mm[2][0], &entry->blue_coef, sizeof(struct csc_vector));

    csc_mm[0][3] = entry->rgb_offset.x * csc_mm[0][0] + entry->rgb_offset.y * csc_mm[0][1] +
                   entry->rgb_offset.z * csc_mm[0][2];

    csc_mm[1][3] = entry->rgb_offset.x * csc_mm[1][0] + entry->rgb_offset.y * csc_mm[1][1] +
                   entry->rgb_offset.z * csc_mm[1][2];

    csc_mm[2][3] = entry->rgb_offset.x * csc_mm[2][0] + entry->rgb_offset.y * csc_mm[2][1] +
                   entry->rgb_offset.z * csc_mm[2][2];

    csc_final[0] = csc_mm[0][0] * bg_color->ycbcra.y + csc_mm[0][1] * bg_color->ycbcra.cb +
                   csc_mm[0][2] * bg_color->ycbcra.cr + csc_mm[0][3];

    csc_final[1] = csc_mm[1][0] * bg_color->ycbcra.y + csc_mm[1][1] * bg_color->ycbcra.cb +
                   csc_mm[1][2] * bg_color->ycbcra.cr + csc_mm[1][3];

    csc_final[2] = csc_mm[2][0] * bg_color->ycbcra.y + csc_mm[2][1] * bg_color->ycbcra.cb +
                   csc_mm[2][2] * bg_color->ycbcra.cr + csc_mm[2][3];

    // switch to RGB components
    bg_color->rgba.a = bg_color->ycbcra.a;
    bg_color->rgba.r = clip_float(csc_final[0]);
    bg_color->rgba.g = clip_float(csc_final[1]);
    bg_color->rgba.b = clip_float(csc_final[2]);
    if ((bg_color->rgba.r != csc_final[0]) || (bg_color->rgba.g != csc_final[1]) ||
        (bg_color->rgba.b != csc_final[2])) {
        output_is_clipped = true;
    }
    bg_color->is_ycbcr = false;
    return output_is_clipped;
}

bool vpe_is_global_bg_blend_applied(struct stream_ctx *stream_ctx)
{

    return (stream_ctx->stream.blend_info.blending)  &&
        (stream_ctx->stream.blend_info.global_alpha) &&
        (stream_ctx->stream.blend_info.global_alpha_value != 1.0);
}

struct gamma_coefs {
    float a0;
    float a1;
    float a2;
    float a3;
    float user_gamma;
    float user_contrast;
    float user_brightness;
};

// srgb, 709, G24
static const int32_t numerator01[] = {31308, 180000, 0};
static const int32_t numerator02[] = {12920, 4500, 0};
static const int32_t numerator03[] = {55, 99, 0};
static const int32_t numerator04[] = {55, 99, 0};
static const int32_t numerator05[] = {2400, 2222, 2400};

static bool build_coefficients(struct gamma_coefs *coefficients, enum color_transfer_func type)
{
    uint32_t index = 0;
    bool     ret   = true;

    if (type == TRANSFER_FUNC_SRGB)
        index = 0;
    else if (type == TRANSFER_FUNC_BT709)
        index = 1;
    else if (type == TRANSFER_FUNC_BT1886)
        index = 2;
    else {
        ret = false;
        goto release;
    }

    coefficients->a0         = (float)numerator01[index] / 10000000.0f;
    coefficients->a1         = (float)numerator02[index] / 1000.0f;
    coefficients->a2         = (float)numerator03[index] / 1000.0f;
    coefficients->a3         = (float)numerator04[index] / 1000.0f;
    coefficients->user_gamma = (float)numerator05[index] / 1000.0f;

release:
    return ret;
}

static double translate_to_linear_space(
    double arg, double a0, double a1, double a2, double a3, double gamma)
{
    double linear;
    double base;

    a0 *= a1;
    if (arg <= -a0) {
        base   = (a2 - arg) / (1.0 + a3);
        linear = -pow(base, gamma);
    } else if ((-a0 <= arg) && (arg <= a0))
        linear = arg / a1;
    else {
        base   = (a2 + arg) / (1.0 + a3);
        linear = pow(base, gamma);
    }

    return linear;
}

// for 709 & sRGB
static void compute_degam(enum color_transfer_func tf, double inY, double *outX, bool clip)
{
    double             ret;
    struct gamma_coefs coefs = {0};

    build_coefficients(&coefs, tf);

    ret = translate_to_linear_space(inY, (double)coefs.a0, (double)coefs.a1, (double)coefs.a2,
        (double)coefs.a3, (double)coefs.user_gamma);

    if (clip) {
        ret = clip_double(ret);
    }
    *outX = ret;
}

static double get_maximum_fp(double a, double b)
{
    if (a > b)
        return a;
    return b;
}

static void compute_depq(double inY, double *outX, bool clip)
{
    double M1 = 0.159301758;
    double M2 = 78.84375;
    double C1 = 0.8359375;
    double C2 = 18.8515625;
    double C3 = 18.6875;

    double nPowM2;
    double base;
    double one      = 1.0;
    double zero     = 0.0;
    bool   negative = false;
    double ret;

    if (inY < zero) {
        inY      = -inY;
        negative = true;
    }
    nPowM2 = pow(inY, one / M2);
    base   = get_maximum_fp(nPowM2 - C1, zero) / (C2 - C3 * nPowM2);
    ret    = pow(base, one / M1);
    if (clip) {
        ret = clip_double(ret);
    }
    if (negative)
        ret = -ret;

    *outX = ret;
}

bool vpe_is_limited_cs(enum color_space cs)
{
    bool is_limited = false;

    switch (cs)
    {
    case COLOR_SPACE_RGB601:
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_2020_RGB_FULLRANGE:
    case COLOR_SPACE_MSREF_SCRGB:
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR_JFIF:
    case COLOR_SPACE_RGB_JFIF:
    case COLOR_SPACE_2020_YCBCR:
        is_limited = false;
        break;
    case COLOR_SPACE_RGB601_LIMITED:
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_YCBCR601_LIMITED:
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        is_limited = true;
        break;
    default:
        VPE_ASSERT(0);
        is_limited = false;
        break;
    }
    return is_limited;
}

void vpe_bg_degam(struct transfer_func *output_tf, struct vpe_color *bg_color)
{

    double degam_r = (double)bg_color->rgba.r;
    double degam_g = (double)bg_color->rgba.g;
    double degam_b = (double)bg_color->rgba.b;

    // de-gam
    switch (output_tf->tf) {

    case TRANSFER_FUNC_PQ2084:
        compute_depq((double)bg_color->rgba.r, &degam_r, true);
        compute_depq((double)bg_color->rgba.g, &degam_g, true);
        compute_depq((double)bg_color->rgba.b, &degam_b, true);
        break;
    case TRANSFER_FUNC_SRGB:
    case TRANSFER_FUNC_BT709:
    case TRANSFER_FUNC_BT1886:
        compute_degam(output_tf->tf, (double)bg_color->rgba.r, &degam_r, true);
        compute_degam(output_tf->tf, (double)bg_color->rgba.g, &degam_g, true);
        compute_degam(output_tf->tf, (double)bg_color->rgba.b, &degam_b, true);
        break;
    case TRANSFER_FUNC_LINEAR:
        break;
    default:
        VPE_ASSERT(0);
        break;
    }
    bg_color->rgba.r = (float)degam_r;
    bg_color->rgba.g = (float)degam_g;
    bg_color->rgba.b = (float)degam_b;
}

void vpe_bg_inverse_gamut_remap(
    enum color_space output_cs, struct transfer_func *output_tf, struct vpe_color *bg_color)
{

        double bg_rgb[3] = { 0.0 };
        double final_bg_rgb[3] = { 0.0 };
        double matrix[9] = { 0.0 };
        bg_rgb[0] = (double)bg_color->rgba.r;
        bg_rgb[1] = (double)bg_color->rgba.g;
        bg_rgb[2] = (double)bg_color->rgba.b;

        switch (output_tf->tf) {
        case TRANSFER_FUNC_LINEAR:
            /* Since linear output uses Bt709, and this conversion is only needed
             * when the tone mapping is enabled on (Bt2020) input, it is needed to
             * apply the reverse of Bt2020 -> Bt709 on the background color to
             * cancel out the effect of Bt2020 -> Bt709 on the background color.
             */
            set_gamut_remap_matrix(matrix, COLOR_SPACE_SRGB, COLOR_SPACE_2020_RGB_FULLRANGE);
            color_multiply_matrices_double(final_bg_rgb, matrix, bg_rgb, 3, 3, 1);

            bg_color->rgba.r = (float)clip_double(final_bg_rgb[0]);
            bg_color->rgba.g = (float)clip_double(final_bg_rgb[1]);
            bg_color->rgba.b = (float)clip_double(final_bg_rgb[2]);

            break;
        case TRANSFER_FUNC_PQ2084:
        case TRANSFER_FUNC_SRGB:
        case TRANSFER_FUNC_BT709:
        case TRANSFER_FUNC_BT1886:
            break;
        default:
            VPE_ASSERT(0);
            break;
        }

}

void vpe_inverse_output_csc(enum color_space output_cs, struct vpe_color *bg_color)
{
    enum color_space bgcolor_cs = COLOR_SPACE_YCBCR709;

    switch (output_cs) {
        // output is ycbr cs, follow output's setting
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR601_LIMITED:
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        bgcolor_cs = output_cs;
        break;
        // output is RGB cs, follow output's range
        // but need yuv to rgb csc
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_RGB601_LIMITED:
        bgcolor_cs = COLOR_SPACE_YCBCR709_LIMITED;
        break;
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
        bgcolor_cs = COLOR_SPACE_2020_YCBCR_LIMITED;
        break;
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_MSREF_SCRGB:
    case COLOR_SPACE_RGB601:
        bgcolor_cs = COLOR_SPACE_YCBCR709;
        break;
    case COLOR_SPACE_2020_RGB_FULLRANGE:
        bgcolor_cs = COLOR_SPACE_2020_YCBCR;
        break;
    default:
        // should revise the newly added CS
        // and set corresponding bgcolor_cs accordingly
        VPE_ASSERT(0);
        bgcolor_cs = COLOR_SPACE_YCBCR709;
        break;
    }

    // input is [0-0xffff]
    // convert bg color to RGB full range for use inside pipe
    vpe_bg_csc(bg_color, bgcolor_cs);
}

struct format_range_csc_table {
    enum color_space      cs;
    enum color_range_type range;
    bool                  bg_color_is_yuv;
    float                 val[12];
};

// Used to invert OCSC and convert into pipeline RGB for MPC bg programming
static const struct format_range_csc_table bgcolor_format_range_inversion[] = {

    // RGB BG Color, RGB output
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_8BPC, false,
        {1.16438356164384f, 0, 0, -0.0730593607305936f, 0, 1.16438356164384f, 0,
            -0.0730593607305936f, 0, 0, 1.16438356164384f, -0.0730593607305936f}},
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_10BPC, false,
        {1.167808219178082f, 0, 0, -0.073059360730594f, 0, 1.167808219178082f, 0,
            -0.073059360730594f, 0, 0, 1.167808219178082f, -0.073059360730594f}},
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_16BPC, false,
        {1.168931934931507f, 0, 0, -0.073059360730594f, 0, 1.168931934931507f, 0,
            -0.073059360730594f, 0, 0, 1.168931934931507f, -0.073059360730594f}},

    // RGB BG Color, YUV output
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_8BPC, false,
        {1.14616407778763f, 0.0152565435443742f, 0.00296294031182745f, -0.0761888250163078f,
            0.00777122064477659f, 1.15364940068723f, 0.00296294031182746f, -0.0706971451680937f,
            0.00777122064477664f, 0.0152565435443742f, 1.14135579745468f, -0.0770147178734506f}},
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_10BPC, false,
        {1.14953514860466f, 0.0153014157312695f, 0.00297165484215636f, -0.0738417268020222f,
            0.00779407717608477f, 1.15704248715984f, 0.00297165484215636f, -0.0724688068399686f,
            0.00779407717608482f, 0.0153014157312695f, 1.14471272627073f, -0.0740482000163079f}},
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_16BPC, false,
        {1.16893193493151f, 0.0f, 0.0f, -0.0570672980878996f, 0.0f, 1.16893193493151f, 0.0f,
            -0.0851306597780037f, 0.0f, 0.0f, 1.16893193493151f, -0.0528468535958906f}},

    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_8BPC, false,
        {1.14391848092026f, 0.0185885518580174f, 0.00187652886555610f, -0.0765745393020221f,
            0.00552562377740495f, 1.15698140900087f, 0.00187652886555609f, -0.0715963059404152f,
            0.00552562377740481f, 0.0185885518580174f, 1.14026938600841f, -0.0772013250163077f}},
    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_10BPC, false,
        {1.14728294704062f, 0.0186432240693645f, 0.00188204806810185f, -0.0739381553734507f,
            0.00554187561204439f, 1.16038429549794f, 0.00188204806810185f, -0.0726935970330490f,
            0.00554187561204425f, 0.0186432240693646f, 1.14362311949667f, -0.0740948518020221f}},
    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_16BPC, false,
        {1.16893193493151f, 0.0f, 0.0f, -0.0550962364440639f, 0.0f, 1.16893193493151f, 0.0f,
            -0.0805358045299480f, 0.0f, 0.0f, 1.16893193493151f, -0.0518932612728311f}},

    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_8BPC, false,
        {1.14522061521626f, 0.0176216976494039f, 0.00154124877817476f, -0.0763508785877364f,
            0.00682775807339991f, 1.15601455479226f, 0.00154124877817475f, -0.0714167128422008f,
            0.00682775807339991f, 0.0176216976494038f, 1.13993410592103f, -0.0772589143020221f}},
    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_10BPC, false,
        {1.14858891114336f, 0.0176735261719021f, 0.00154578186281643f, -0.0738822401948792f,
            0.00684783971479236f, 1.15941459760047f, 0.00154578186281642f, -0.0726486987584954f,
            0.00684783971479225f, 0.0176735261719021f, 1.14328685329139f, -0.0741092491234507f}},
    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_16BPC, false,
        {1.16893193493151f, 0.0f, 0.0f, -0.0562391784389270f, 0.0f, 1.16893193493151f, 0.0f,
            -0.0814535539639162f, 0.0f, 0.0f, 1.16893193493151f, -0.0515989708190638f}},

    // YUV BG Color, YUV output
    {COLOR_SPACE_YCBCR601, COLOR_RANGE_FULL, true,
        {1.0f, 0, 1.4020f, -0.7010f, 1.0f, -0.3441362860f, -0.7141362860f, 0.5291362860f, 1.0f,
            1.7720f, 0, -0.8860f}},
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.59602678571429f, -0.874202217873451f, 1.16438356164384f,
            -0.391762289866072f, -0.812967647008929f, 0.531667823269406f, 1.16438356164384f,
            2.01723214285714f, 0, -1.08563078930202f}},
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.60072098214286f, -0.874202217873451f, 1.16780821917808f,
            -0.392914531895089f, -0.815358728323661f, 0.531667823269406f, 1.16780821917808f,
            2.02316517857143f, 0, -1.08563078930202f}},
    {COLOR_SPACE_YCBCR601_LIMITED, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.63884257277397f, -0.876488584474886f, 1.16893193493151f,
            -0.402271894674122f, -0.834776710598780f, 0.533393642858448f, 1.16893193493151f,
            2.07134738869863f, 0, -1.08852054794521f}},

    {COLOR_SPACE_YCBCR709, COLOR_RANGE_FULL, true,
       {1.0f, 0, 1.57480f, -0.78740f, 1.0f, -0.1873242730f, -0.4681242730f, 0.3277242730f, 1.0f,
            1.85560f, 0, -0.92780f}},
    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.79274107142857f, -0.972945075016308f, 1.16438356164384f,
            -0.213248614352679f, -0.532909328638393f, 0.301482665555121f, 1.16438356164384f,
            2.11240178571429f, 0, -1.13340221787345f}},
    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.79801383928571f, -0.972945075016308f, 1.16780821917808f,
            -0.213875816159598f, -0.534476709016741f, 0.301482665555121f, 1.16780821917808f,
            2.11861473214286f, 0, -1.13340221787345f}},
    {COLOR_SPACE_YCBCR709_LIMITED, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.84083401113014f, -0.975513242009132f, 1.16893193493151f,
            -0.218969324897528f, -0.547205412226295f, 0.302551564031963f, 1.16893193493151f,
            2.16907009845890f, 0, -1.13642831050228f}},

    {COLOR_SPACE_2020_YCBCR, COLOR_RANGE_FULL, true,
        {1.0f, 0, 1.47460f, -0.73730f, 1.0f, -0.1645531270f, -0.5713531270f, 0.3679531270f, 1.0f,
            1.88140f, 0, -0.94070f}},
    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.67867410714286f, -0.915687932159165f, 1.16438356164384f,
            -0.187326104397321f, -0.650424318683036f, 0.347458498697978f, 1.16438356164384f,
            2.14177232142857f, 0, -1.14814507501631f}},
    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.68361138392857f, -0.915687932159165f, 1.16780821917808f,
            -0.187877063527902f, -0.652337331385045f, 0.347458498697978f, 1.16780821917808f,
            2.14807165178571f, 0, -1.14814507501631f}},
    {COLOR_SPACE_2020_YCBCR_LIMITED, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.723707031250f, -0.918092694063927f, 1.16893193493151f,
            -0.192351405143140f, -0.667872916273277f, 0.348658606744292f, 1.16893193493151f,
            2.19922854238014f, 0, -1.15121324200913f}},


    // YUV BG Colorf, RGB output
    {COLOR_SPACE_RGB601, COLOR_RANGE_FULL, true,
        {1.0f, 0, 1.4020f, -0.7010f, 1.0f, -0.3441362860f, -0.7141362860f, 0.5291362860f, 1.0f,
            1.7720f, 0, -0.8860f}},
    {COLOR_SPACE_RGB601_LIMITED, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.63246575342466f, -0.889292237442922f, 1.16438356164384f,
            -0.400706634383562f, -0.831528552191781f, 0.543058232557078f, 1.16438356164384f,
            2.06328767123288f, 0, -1.10470319634703f}},
    {COLOR_SPACE_RGB601_LIMITED, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.63726712328767f, -0.891692922374429f, 1.16780821917808f,
            -0.401885183308219f, -0.833974224404110f, 0.544870343125571f, 1.16780821917808f,
            2.06935616438356f, 0, -1.10773744292237f}},
    {COLOR_SPACE_RGB601_LIMITED, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.63884257277397f, -0.892480647117580f, 1.16893193493151f,
            -0.402271894674122f, -0.834776710598780f, 0.545464941905858f, 1.16893193493151f,
            2.07134738869863f, 0, -1.10873305507991f}},

    {COLOR_SPACE_SRGB, COLOR_RANGE_FULL, true,
        {1.0f, 0, 1.57480f, -0.78740f, 1.0f, -0.1873242730f, -0.4681242730f, 0.3277242730f, 1.0f,
            1.85560f, 0, -0.92780f}},
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.83367123287671f, -0.989894977168950f, 1.16438356164384f,
            -0.218117304178082f, -0.545076208287671f, 0.308537395502283f, 1.16438356164384f,
            2.16063013698630f, 0, -1.15337442922374f}},
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.83906438356164f, -0.992591552511415f, 1.16780821917808f,
            -0.218758825660959f, -0.546679373606164f, 0.309659738902968f, 1.16780821917808f,
            2.16698493150685f, 0, -1.15655182648402f}},
    {COLOR_SPACE_SRGB_LIMITED, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.84083401113014f, -0.993476366295662f, 1.16893193493151f,
            -0.218969324897528f, -0.547205412226295f, 0.310028007831318f, 1.16893193493151f,
            2.16907009845890f, 0, -1.15759440996005f}},

    {COLOR_SPACE_2020_RGB_FULLRANGE, COLOR_RANGE_FULL, true,
        {1.0f, 0, 1.47460f, -0.73730f, 1.0f, -0.1645531270f, -0.5713531270f, 0.3679531270f, 1.0f,
            1.88140f, 0, -0.94070f}},
    {COLOR_SPACE_2020_RGB_LIMITEDRANGE, COLOR_RANGE_LIMITED_8BPC, true,
        {1.16438356164384f, 0, 1.7170f, -0.931559360730594f, 1.16438356164384f, -0.191602956095890f,
            -0.665274188972603f, 0.355379211803653f, 1.16438356164384f, 2.19067123287671f, 0,
            -1.16839497716895f}},
    {COLOR_SPACE_2020_RGB_LIMITEDRANGE, COLOR_RANGE_LIMITED_10BPC, true,
        {1.16780821917808f, 0, 1.722050f, -0.934084360730593f, 1.16780821917808f,
            -0.192166494202055f, -0.667230877763699f, 0.356639325252283f, 1.16780821917808f,
            2.19711438356164f, 0, -1.17161655251142f}},
    {COLOR_SPACE_2020_RGB_LIMITEDRANGE, COLOR_RANGE_LIMITED_16BPC, true,
        {1.16893193493151f, 0, 1.723707031250f, -0.934912876355594f, 1.16893193493151f,
            -0.192351405143140f, -0.667872916273277f, 0.357052799977615f, 1.16893193493151f,
            2.19922854238014f, 0, -1.17267363192066f}}};

// Used to convert bg into output cs for OPP programming (so always in full range)
static const struct format_range_csc_table bgcolor_color_space_table[] = {

    // RGB BG Color, YUV output
    {COLOR_SPACE_YCBCR601, COLOR_RANGE_FULL, false,
        {0.500000000027881f, -0.418687589221465f, -0.0813124108064158f, 0.5f, 0.298999999960911f,
            0.587000000088494f, 0.113999999950595f, 0.0f, -0.168735891625796f, -0.331264108402085f,
            0.500000000027881f, 0.5f}},
    {COLOR_SPACE_YCBCR709, COLOR_RANGE_FULL, false,
        {0.499999999987861f, -0.454152908279373f, -0.0458470917084874f, 0.5f, 0.212600000019117f,
            0.715199999958357f, 0.0722000000225260f, 0.0f, -0.114572106067642f, -0.385427893920218f,
            0.499999999987861f, 0.5f}},
    {COLOR_SPACE_2020_YCBCR, COLOR_RANGE_FULL, false,
        {0.499999999974095f, -0.459785704538901f, -0.0402142954351941f, 0.5f, 0.262700000038199f,
            0.677999999913064f, 0.0593000000487373f, 0.0f, -0.139630062739555f, -0.360369937234540f,
            0.499999999974095f, 0.5f}},

    // YUV BG Color, RGB output
    {COLOR_SPACE_RGB601, COLOR_RANGE_FULL, true,
        {1.0f, 0.0f, 1.40200000000000f, -0.701000000000000f, 1.0f, -0.344136286000000f,
            -0.714136286000000f, 0.529136286000000f, 1.0f, 1.77200000000000f, 0.0f,
            -0.886000000000000f}},
    {COLOR_SPACE_SRGB, COLOR_RANGE_FULL, true,
        {1.0f, 0.0f, 1.57480000000000f, -0.787400000000000f, 1.0f, -0.187324273000000f,
            -0.468124273000000f, 0.327724273000000f, 1.0f, 1.85560000000000f, 0.0f,
            -0.927800000000000f}},
    {COLOR_SPACE_2020_RGB_FULLRANGE, COLOR_RANGE_FULL, true,
        {1.0f, 0.0f, 1.47460000000000f, -0.737300000000000f, 1.0f, -0.164553127000000f,
            -0.571353127000000f, 0.367953127000000f, 1.0f, 1.88140000000000f, 0.0f,
            -0.940700000000000f}}};

bool vpe_is_yuv_cs(enum color_space cs)
{
    switch (cs) {
        // output is ycbr cs, follow output's setting
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR601_LIMITED:
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        return true;
    default:
        return false;
    }
}

void vpe_bg_format_and_limited_conversion(enum color_space output_cs,
    enum vpe_surface_pixel_format pixel_format, struct vpe_color *bg_color)
{
    // for limited YUV output, OCSC does studio conversion. Need to calculate RGB value that will
    // convert to full range YUV out, after OCSC full-> studio conversion
    enum color_range_type range = vpe_get_range_type(output_cs, pixel_format);
    enum color_space      cs    = output_cs;

    if (!vpe_is_yuv_cs(output_cs) && !bg_color->is_ycbcr)
        // if RGB BG->RGB out we do not care about which chromaticity to use as in-pipe RGB is
        // typeless
        cs = COLOR_SPACE_SRGB_LIMITED;
    else
        // map certain color spaces to their matching one in the matrix array
        switch (cs) {
        case COLOR_SPACE_MSREF_SCRGB:
            cs = COLOR_SPACE_SRGB;
            break;
        case COLOR_SPACE_YCBCR_JFIF:
            cs = COLOR_SPACE_YCBCR601;
            break;
        case COLOR_SPACE_RGB_JFIF:
            cs = COLOR_SPACE_RGB601;
            break;
        default:
            break;
        }

    int i;
    int arr_size = 12;
    for (i = 0; i < arr_size; i++)
        if (bgcolor_format_range_inversion[i].cs == cs &&
            bgcolor_format_range_inversion[i].range == range &&
            bgcolor_format_range_inversion[i].bg_color_is_yuv == bg_color->is_ycbcr) {
            const float          *m    = bgcolor_format_range_inversion[i].val;

            if (bg_color->is_ycbcr) {
                struct vpe_color_ycbcra ycbcra = bg_color->ycbcra;
                bg_color->rgba.r = ycbcra.y * m[0] + ycbcra.cb * m[1] + ycbcra.cr * m[2] + m[3];
                bg_color->rgba.g = ycbcra.y * m[4] + ycbcra.cb * m[5] + ycbcra.cr * m[6] + m[7];
                bg_color->rgba.b = ycbcra.y * m[8] + ycbcra.cb * m[9] + ycbcra.cr * m[10] + m[11];
            } else {
                struct vpe_color_rgba rgba = bg_color->rgba;
                bg_color->rgba.r = rgba.r * m[0] + rgba.g * m[1] + rgba.b * m[2] + m[3];
                bg_color->rgba.g = rgba.r * m[4] + rgba.g * m[5] + rgba.b * m[6] + m[7];
                bg_color->rgba.b = rgba.r * m[8] + rgba.g * m[9] + rgba.b * m[10] + m[11];
            }
            break;
        }
}

void vpe_bg_color_space_conversion(enum color_space output_cs, struct vpe_color *bg_color)
{
    enum color_space cs = output_cs;

    // map certain color spaces to their matching one in the matrix array
    switch (cs) {
    case COLOR_SPACE_RGB601:
    case COLOR_SPACE_RGB601_LIMITED:
    case COLOR_SPACE_RGB_JFIF:
        cs = COLOR_SPACE_RGB601;
        break;
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_MSREF_SCRGB:
        cs = COLOR_SPACE_SRGB;
        break;
    case COLOR_SPACE_2020_RGB_FULLRANGE:
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
        cs = COLOR_SPACE_2020_RGB_FULLRANGE;
        break;
    case COLOR_SPACE_YCBCR601_LIMITED:
    case COLOR_SPACE_YCBCR_JFIF:
    case COLOR_SPACE_YCBCR601:
        cs = COLOR_SPACE_YCBCR601;
        break;
    case COLOR_SPACE_YCBCR709_LIMITED:
    case COLOR_SPACE_YCBCR709:
        cs = COLOR_SPACE_YCBCR709;
        break;
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        cs = COLOR_SPACE_2020_YCBCR;
        break;
    default:
        break;
    }

    int i;
    int arr_size = 6;
    for (i = 0; i < arr_size; i++)
        if (bgcolor_color_space_table[i].cs == cs &&
            bgcolor_color_space_table[i].bg_color_is_yuv == bg_color->is_ycbcr) {
            const float *m = bgcolor_color_space_table[i].val;

            if (vpe_is_yuv_cs(cs)) {
                struct vpe_color_rgba rgba = bg_color->rgba;
                bg_color->ycbcra.cr        = rgba.r * m[0] + rgba.g * m[1] + rgba.b * m[2] + m[3];
                bg_color->ycbcra.y         = rgba.r * m[4] + rgba.g * m[5] + rgba.b * m[6] + m[7];
                bg_color->ycbcra.cb        = rgba.r * m[8] + rgba.g * m[9] + rgba.b * m[10] + m[11];
                bg_color->is_ycbcr         = true;
            } else {
                struct vpe_color_ycbcra ycbcra = bg_color->ycbcra;
                bg_color->rgba.r   = ycbcra.y * m[0] + ycbcra.cb * m[1] + ycbcra.cr * m[2] + m[3];
                bg_color->rgba.g   = ycbcra.y * m[4] + ycbcra.cb * m[5] + ycbcra.cr * m[6] + m[7];
                bg_color->rgba.b   = ycbcra.y * m[8] + ycbcra.cb * m[9] + ycbcra.cr * m[10] + m[11];
                bg_color->is_ycbcr = false;
            }
            break;
        }
}
