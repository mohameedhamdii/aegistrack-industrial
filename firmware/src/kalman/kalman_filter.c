/**
 * @file kalman_filter.c
 * @brief 4-State Linear Kalman Filter Implementation
 *
 * Implements predict and update steps for a constant-velocity model.
 * All matrix operations are done inline (no external math library)
 * to keep the firmware lean.
 *
 * State model:
 *   x_k = F * x_{k-1} + w
 *   z_k = H * x_k + v
 *
 * F = [1  0  dt  0 ]    H = [1  0  0  0]
 *     [0  1  0   dt]        [0  1  0  0]
 *     [0  0  1   0 ]
 *     [0  0  0   1 ]
 */

#include "kalman_filter.h"
#include <math.h>
#include <string.h>

/* ──────────────────────────── Constants ──────────────────────────── */

#define DEG_TO_M_LAT  111320.0f   /**< Meters per degree latitude  */
#define DEG_TO_RAD    0.01745329f
#define PI_F          3.14159265f

/* ──────────────────────────── Matrix Helpers ──────────────────────── */

/** Zero a 4×4 matrix. */
static void mat4_zero(kf_mat4_t *m)
{
    memset(m, 0, sizeof(kf_mat4_t));
}

/** Set a 4×4 matrix to identity. */
static void mat4_identity(kf_mat4_t *m)
{
    mat4_zero(m);
    for (int i = 0; i < KF_STATE_DIM; i++) {
        m->m[i][i] = 1.0f;
    }
}

/** Multiply two 4×4 matrices: C = A * B. */
static void mat4_mul(const kf_mat4_t *A, const kf_mat4_t *B, kf_mat4_t *C)
{
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_STATE_DIM; j++) {
            float sum = 0.0f;
            for (int k = 0; k < KF_STATE_DIM; k++) {
                sum += A->m[i][k] * B->m[k][j];
            }
            C->m[i][j] = sum;
        }
    }
}

/** Transpose a 4×4 matrix: B = A^T. */
static void mat4_transpose(const kf_mat4_t *A, kf_mat4_t *B)
{
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_STATE_DIM; j++) {
            B->m[j][i] = A->m[i][j];
        }
    }
}

/** Add two 4×4 matrices: C = A + B. */
static void mat4_add(const kf_mat4_t *A, const kf_mat4_t *B, kf_mat4_t *C)
{
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_STATE_DIM; j++) {
            C->m[i][j] = A->m[i][j] + B->m[i][j];
        }
    }
}

/** Multiply 4×4 matrix by 4-vector: y = A * x. */
static void mat4_vec_mul(const kf_mat4_t *A, const kf_vec4_t *x, kf_vec4_t *y)
{
    for (int i = 0; i < KF_STATE_DIM; i++) {
        float sum = 0.0f;
        for (int j = 0; j < KF_STATE_DIM; j++) {
            sum += A->m[i][j] * x->v[j];
        }
        y->v[i] = sum;
    }
}

/** Invert a 2×2 matrix. Returns 0 on success, -1 if singular. */
static int mat2_invert(const kf_mat2_t *A, kf_mat2_t *Ainv)
{
    float det = A->m[0][0] * A->m[1][1] - A->m[0][1] * A->m[1][0];
    if (fabsf(det) < 1e-30f) {
        return -1;  /* Singular matrix */
    }
    float inv_det = 1.0f / det;
    Ainv->m[0][0] =  A->m[1][1] * inv_det;
    Ainv->m[0][1] = -A->m[0][1] * inv_det;
    Ainv->m[1][0] = -A->m[1][0] * inv_det;
    Ainv->m[1][1] =  A->m[0][0] * inv_det;
    return 0;
}

/* ──────────────────────────── Public API ──────────────────────────── */

void kf_init(kalman_filter_t *kf,
             float initial_lat, float initial_lon,
             float q_pos, float q_vel,
             float r_pos)
{
    memset(kf, 0, sizeof(kalman_filter_t));

    /* Initial state: known position, zero velocity */
    kf->x.v[0] = initial_lat;
    kf->x.v[1] = initial_lon;
    kf->x.v[2] = 0.0f;  /* v_lat */
    kf->x.v[3] = 0.0f;  /* v_lon */

    /* Initial error covariance — high uncertainty in velocity */
    mat4_zero(&kf->P);
    kf->P.m[0][0] = r_pos;      /* Position uncertainty */
    kf->P.m[1][1] = r_pos;
    kf->P.m[2][2] = q_vel * 10; /* High velocity uncertainty */
    kf->P.m[3][3] = q_vel * 10;

    /* Process noise — tuned for expected asset dynamics */
    mat4_zero(&kf->Q);
    kf->Q.m[0][0] = q_pos;
    kf->Q.m[1][1] = q_pos;
    kf->Q.m[2][2] = q_vel;
    kf->Q.m[3][3] = q_vel;

    /* Measurement noise — from GPS accuracy */
    kf->R.m[0][0] = r_pos;
    kf->R.m[0][1] = 0.0f;
    kf->R.m[1][0] = 0.0f;
    kf->R.m[1][1] = r_pos;

    /* Measurement matrix H (constant) */
    memset(&kf->H, 0, sizeof(kf_mat2x4_t));
    kf->H.m[0][0] = 1.0f;  /* z0 = lat */
    kf->H.m[1][1] = 1.0f;  /* z1 = lon */

    /* State transition matrix F (updated each predict step with dt) */
    mat4_identity(&kf->F);

    kf->initialized = true;
}

void kf_predict(kalman_filter_t *kf, float dt)
{
    if (!kf->initialized) return;

    /* Update F matrix with current dt:
     * F = [1  0  dt  0 ]
     *     [0  1  0   dt]
     *     [0  0  1   0 ]
     *     [0  0  0   1 ]
     */
    kf->F.m[0][2] = dt;
    kf->F.m[1][3] = dt;

    /* x_predicted = F * x */
    kf_vec4_t x_pred;
    mat4_vec_mul(&kf->F, &kf->x, &x_pred);
    kf->x = x_pred;

    /* P_predicted = F * P * F^T + Q */
    kf_mat4_t F_T, FP, FPFt;
    mat4_transpose(&kf->F, &F_T);
    mat4_mul(&kf->F, &kf->P, &FP);
    mat4_mul(&FP, &F_T, &FPFt);
    mat4_add(&FPFt, &kf->Q, &kf->P);
}

void kf_update(kalman_filter_t *kf, float meas_lat, float meas_lon)
{
    if (!kf->initialized) return;

    /* Innovation (measurement residual): y = z - H * x */
    float y[KF_MEAS_DIM];
    y[0] = meas_lat - kf->x.v[0];
    y[1] = meas_lon - kf->x.v[1];

    /* Innovation covariance: S = H * P * H^T + R */
    /* Since H = [I_2x2 | 0_2x2], this simplifies to:
     * S = P[0:2,0:2] + R */
    kf_mat2_t S;
    S.m[0][0] = kf->P.m[0][0] + kf->R.m[0][0];
    S.m[0][1] = kf->P.m[0][1] + kf->R.m[0][1];
    S.m[1][0] = kf->P.m[1][0] + kf->R.m[1][0];
    S.m[1][1] = kf->P.m[1][1] + kf->R.m[1][1];

    /* Kalman gain: K = P * H^T * S^(-1) */
    kf_mat2_t S_inv;
    if (mat2_invert(&S, &S_inv) != 0) {
        return;  /* Singular — skip update */
    }

    /* K is 4×2: K = P * H^T * S_inv
     * Since H^T = [I_2x2; 0_2x2], P * H^T = P[:,0:2] */
    float K[KF_STATE_DIM][KF_MEAS_DIM];
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_MEAS_DIM; j++) {
            K[i][j] = 0.0f;
            for (int k = 0; k < KF_MEAS_DIM; k++) {
                K[i][j] += kf->P.m[i][k] * S_inv.m[k][j];
            }
        }
    }

    /* Update state: x = x + K * y */
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_MEAS_DIM; j++) {
            kf->x.v[i] += K[i][j] * y[j];
        }
    }

    /* Update error covariance: P = (I - K * H) * P */
    /* KH is 4×4 */
    kf_mat4_t KH;
    mat4_zero(&KH);
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_STATE_DIM; j++) {
            for (int k = 0; k < KF_MEAS_DIM; k++) {
                KH.m[i][j] += K[i][k] * kf->H.m[k][j];
            }
        }
    }

    /* I - KH */
    kf_mat4_t IKH;
    mat4_identity(&IKH);
    for (int i = 0; i < KF_STATE_DIM; i++) {
        for (int j = 0; j < KF_STATE_DIM; j++) {
            IKH.m[i][j] -= KH.m[i][j];
        }
    }

    /* P = (I - KH) * P */
    kf_mat4_t P_new;
    mat4_mul(&IKH, &kf->P, &P_new);
    kf->P = P_new;
}

void kf_get_position(const kalman_filter_t *kf, float *lat, float *lon)
{
    *lat = kf->x.v[0];
    *lon = kf->x.v[1];
}

void kf_get_velocity(const kalman_filter_t *kf, float *v_lat, float *v_lon)
{
    *v_lat = kf->x.v[2];
    *v_lon = kf->x.v[3];
}

float kf_get_speed_ms(const kalman_filter_t *kf, float lat)
{
    /* Convert degree/s velocities to m/s */
    float m_per_deg_lon = DEG_TO_M_LAT * cosf(lat * DEG_TO_RAD);
    float v_n = kf->x.v[2] * DEG_TO_M_LAT;    /* North velocity in m/s */
    float v_e = kf->x.v[3] * m_per_deg_lon;    /* East velocity in m/s  */
    return sqrtf(v_n * v_n + v_e * v_e);
}

float kf_get_innovation(const kalman_filter_t *kf, float meas_lat, float meas_lon)
{
    float dy_lat = meas_lat - kf->x.v[0];
    float dy_lon = meas_lon - kf->x.v[1];
    return sqrtf(dy_lat * dy_lat + dy_lon * dy_lon);
}

void kf_reset(kalman_filter_t *kf, float lat, float lon)
{
    kf->x.v[0] = lat;
    kf->x.v[1] = lon;
    kf->x.v[2] = 0.0f;
    kf->x.v[3] = 0.0f;

    /* Reset covariance to high uncertainty */
    mat4_zero(&kf->P);
    kf->P.m[0][0] = kf->R.m[0][0];
    kf->P.m[1][1] = kf->R.m[1][1];
    kf->P.m[2][2] = kf->Q.m[2][2] * 10;
    kf->P.m[3][3] = kf->Q.m[3][3] * 10;
}
