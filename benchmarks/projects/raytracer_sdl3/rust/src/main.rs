use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicI32, Ordering};
use std::time::Instant;
use rayon::prelude::*;

const WIDTH: usize = 1280;
const HEIGHT: usize = 720;
const SAMPLES_PER_PIXEL: usize = 16;
const MAX_DEPTH: usize = 8;
const NUM_SPHERES: usize = 124;

// ----------------------------------------------------------------------------
// SDL3 C FFI Bindings
// ----------------------------------------------------------------------------
#[repr(C)]
pub struct SDL_Window { _private: [u8; 0] }
#[repr(C)]
pub struct SDL_Renderer { _private: [u8; 0] }
#[repr(C)]
pub struct SDL_Texture { _private: [u8; 0] }

#[repr(C)]
pub struct SDL_Rect {
    pub x: c_int,
    pub y: c_int,
    pub w: c_int,
    pub h: c_int,
}

#[repr(C)]
pub struct SDL_FRect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

#[repr(C)]
pub struct SDL_Event {
    pub type_: u32,
    pub padding: [u8; 128],
}

const SDL_INIT_VIDEO: u32 = 0x00000020;
const SDL_PIXELFORMAT_RGBA32: c_int = 376840196;
const SDL_TEXTUREACCESS_STREAMING: c_int = 1;
const SDL_EVENT_QUIT: u32 = 0x100;
const SDL_EVENT_KEY_DOWN: u32 = 0x300;

#[link(name = "SDL3")]
extern "C" {
    fn SDL_Init(flags: u32) -> bool;
    fn SDL_Quit();
    fn SDL_GetError() -> *const c_char;
    fn SDL_CreateWindowAndRenderer(
        title: *const c_char,
        width: c_int,
        height: c_int,
        window_flags: u64,
        window: *mut *mut SDL_Window,
        renderer: *mut *mut SDL_Renderer,
    ) -> bool;
    fn SDL_DestroyWindow(window: *mut SDL_Window);
    fn SDL_DestroyRenderer(renderer: *mut SDL_Renderer);
    fn SDL_CreateTexture(
        renderer: *mut SDL_Renderer,
        format: c_int,
        access: c_int,
        w: c_int,
        h: c_int,
    ) -> *mut SDL_Texture;
    fn SDL_DestroyTexture(texture: *mut SDL_Texture);
    fn SDL_LockTexture(
        texture: *mut SDL_Texture,
        rect: *const SDL_Rect,
        pixels: *mut *mut c_void,
        pitch: *mut c_int,
    ) -> bool;
    fn SDL_UnlockTexture(texture: *mut SDL_Texture);
    fn SDL_RenderTexture(
        renderer: *mut SDL_Renderer,
        texture: *mut SDL_Texture,
        srcrect: *const SDL_FRect,
        dstrect: *const SDL_FRect,
    ) -> bool;
    fn SDL_RenderPresent(renderer: *mut SDL_Renderer) -> bool;
    fn SDL_SetRenderVSync(renderer: *mut SDL_Renderer, vsync: c_int) -> bool;
    fn SDL_PollEvent(event: *mut SDL_Event) -> bool;
}

// ----------------------------------------------------------------------------
// Deterministic Math & PRNG
// ----------------------------------------------------------------------------
#[inline(always)]
fn xorshift32(state: &mut u32) -> u32 {
    let mut x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    x
}

#[inline(always)]
fn rand_float(state: &mut u32) -> f32 {
    let u = xorshift32(state);
    (u & 16777215) as f32 * (1.0 / 16777216.0)
}

#[derive(Copy, Clone, Default)]
struct Vec3 {
    x: f32,
    y: f32,
    z: f32,
}

impl Vec3 {
    #[inline(always)]
    fn new(x: f32, y: f32, z: f32) -> Self { Vec3 { x, y, z } }
    #[inline(always)]
    fn add(self, o: Vec3) -> Vec3 { Vec3::new(self.x + o.x, self.y + o.y, self.z + o.z) }
    #[inline(always)]
    fn sub(self, o: Vec3) -> Vec3 { Vec3::new(self.x - o.x, self.y - o.y, self.z - o.z) }
    #[inline(always)]
    fn mul_vec(self, o: Vec3) -> Vec3 { Vec3::new(self.x * o.x, self.y * o.y, self.z * o.z) }
    #[inline(always)]
    fn mul_scalar(self, s: f32) -> Vec3 { Vec3::new(self.x * s, self.y * s, self.z * s) }
    #[inline(always)]
    fn div_scalar(self, s: f32) -> Vec3 { Vec3::new(self.x / s, self.y / s, self.z / s) }
    #[inline(always)]
    fn neg(self) -> Vec3 { Vec3::new(-self.x, -self.y, -self.z) }
    #[inline(always)]
    fn dot(self, o: Vec3) -> f32 { self.x * o.x + self.y * o.y + self.z * o.z }
    #[inline(always)]
    fn cross(self, o: Vec3) -> Vec3 {
        Vec3::new(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )
    }
    #[inline(always)]
    fn normalize(self) -> Vec3 {
        let len = self.dot(self).sqrt();
        if len > 0.0 { self.mul_scalar(1.0 / len) } else { Vec3::default() }
    }
}

#[inline(always)]
fn random_unit_vector(rng: &mut u32) -> Vec3 {
    let u1 = rand_float(rng);
    let u2 = rand_float(rng);
    let z = 1.0 - 2.0 * u1;
    let r = (1.0 - z * z).max(0.0).sqrt();
    let phi = 6.283185307179586 * u2;
    Vec3::new(r * phi.cos(), r * phi.sin(), z)
}

#[inline(always)]
fn reflect(v: Vec3, n: Vec3) -> Vec3 {
    v.sub(n.mul_scalar(2.0 * v.dot(n)))
}

#[inline(always)]
fn refract(uv: Vec3, n: Vec3, etai_over_etat: f32) -> Vec3 {
    let cos_theta = uv.neg().dot(n).min(1.0);
    let r_out_perp = uv.add(n.mul_scalar(cos_theta)).mul_scalar(etai_over_etat);
    let r_out_parallel = n.mul_scalar(-(1.0 - r_out_perp.dot(r_out_perp)).abs().sqrt());
    r_out_perp.add(r_out_parallel)
}

#[derive(Copy, Clone)]
struct Ray {
    origin: Vec3,
    dir: Vec3,
}

#[derive(Default)]
struct HitRecord {
    p: Vec3,
    normal: Vec3,
    t: f32,
    front_face: bool,
    mat_type: i32,
    albedo: Vec3,
    fuzz: f32,
    ior: f32,
}

// ----------------------------------------------------------------------------
// Scene Structs: AoS and SoA
// ----------------------------------------------------------------------------
#[derive(Clone, Copy)]
struct SphereAoS {
    center: Vec3,
    radius: f32,
    inv_radius: f32,
    albedo: Vec3,
    fuzz: f32,
    ior: f32,
    mat_type: i32,
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum Layout {
    Soa,
    Simd,
    Aos,
}

struct SphereSceneSoA {
    cx: [f32; 128],
    cy: [f32; 128],
    cz: [f32; 128],
    radius: [f32; 128],
    inv_radius: [f32; 128],
    rsq: [f32; 128],
    albedo_r: [f32; 128],
    albedo_g: [f32; 128],
    albedo_b: [f32; 128],
    fuzz: [f32; 128],
    ior: [f32; 128],
    mat_type: [i32; 128],
}

fn init_scene() -> (SphereSceneSoA, Vec<SphereAoS>) {
    let mut soa = SphereSceneSoA {
        cx: [0.0; 128], cy: [0.0; 128], cz: [0.0; 128],
        radius: [0.0; 128], inv_radius: [0.0; 128], rsq: [0.0; 128],
        albedo_r: [0.0; 128], albedo_g: [0.0; 128], albedo_b: [0.0; 128],
        fuzz: [0.0; 128], ior: [0.0; 128], mat_type: [0; 128],
    };

    // 0: Ground
    soa.cx[0] = 0.0; soa.cy[0] = -1000.0; soa.cz[0] = 0.0;
    soa.radius[0] = 1000.0; soa.inv_radius[0] = 0.001;
    soa.mat_type[0] = 0;
    soa.albedo_r[0] = 0.5; soa.albedo_g[0] = 0.5; soa.albedo_b[0] = 0.5;
    soa.fuzz[0] = 0.0; soa.ior[0] = 1.0;

    // 1: Hero Glass
    soa.cx[1] = 0.0; soa.cy[1] = 1.0; soa.cz[1] = 0.0;
    soa.radius[1] = 1.0; soa.inv_radius[1] = 1.0;
    soa.mat_type[1] = 2;
    soa.albedo_r[1] = 1.0; soa.albedo_g[1] = 1.0; soa.albedo_b[1] = 1.0;
    soa.fuzz[1] = 0.0; soa.ior[1] = 1.5;

    // 2: Hero Diffuse
    soa.cx[2] = -4.0; soa.cy[2] = 1.0; soa.cz[2] = 0.0;
    soa.radius[2] = 1.0; soa.inv_radius[2] = 1.0;
    soa.mat_type[2] = 0;
    soa.albedo_r[2] = 0.4; soa.albedo_g[2] = 0.2; soa.albedo_b[2] = 0.1;
    soa.fuzz[2] = 0.0; soa.ior[2] = 1.0;

    // 3: Hero Metal
    soa.cx[3] = 4.0; soa.cy[3] = 1.0; soa.cz[3] = 0.0;
    soa.radius[3] = 1.0; soa.inv_radius[3] = 1.0;
    soa.mat_type[3] = 1;
    soa.albedo_r[3] = 0.7; soa.albedo_g[3] = 0.6; soa.albedo_b[3] = 0.5;
    soa.fuzz[3] = 0.0; soa.ior[3] = 1.0;

    // 4..123: 120 procedural spheres
    let mut idx = 4;
    for a in -7..=7 {
        for b in -4..=3 {
            let mut rng_state = ((a + 10) * 100 + (b + 10)) as u32;
            let rx = rand_float(&mut rng_state);
            let rz = rand_float(&mut rng_state);
            let center_x = a as f32 * 1.6 + 0.9 * rx;
            let mut center_z = b as f32 * 1.6 + 0.9 * rz;
            let center_y = 0.2f32;
            let rad = 0.2f32;

            let d1 = center_x * center_x + center_z * center_z;
            let d2 = (center_x + 4.0) * (center_x + 4.0) + center_z * center_z;
            let d3 = (center_x - 4.0) * (center_x - 4.0) + center_z * center_z;
            if d1 < 1.44 || d2 < 1.44 || d3 < 1.44 {
                center_z += 1.5;
            }

            soa.cx[idx] = center_x;
            soa.cy[idx] = center_y;
            soa.cz[idx] = center_z;
            soa.radius[idx] = rad;
            soa.inv_radius[idx] = 1.0 / rad;

            let choose_mat = rand_float(&mut rng_state);
            if choose_mat < 0.70 {
                soa.mat_type[idx] = 0;
                soa.albedo_r[idx] = rand_float(&mut rng_state) * rand_float(&mut rng_state);
                soa.albedo_g[idx] = rand_float(&mut rng_state) * rand_float(&mut rng_state);
                soa.albedo_b[idx] = rand_float(&mut rng_state) * rand_float(&mut rng_state);
                soa.fuzz[idx] = 0.0;
                soa.ior[idx] = 1.0;
            } else if choose_mat < 0.90 {
                soa.mat_type[idx] = 1;
                soa.albedo_r[idx] = 0.5 * (1.0 + rand_float(&mut rng_state));
                soa.albedo_g[idx] = 0.5 * (1.0 + rand_float(&mut rng_state));
                soa.albedo_b[idx] = 0.5 * (1.0 + rand_float(&mut rng_state));
                soa.fuzz[idx] = 0.5 * rand_float(&mut rng_state);
                soa.ior[idx] = 1.0;
            } else {
                soa.mat_type[idx] = 2;
                soa.albedo_r[idx] = 1.0;
                soa.albedo_g[idx] = 1.0;
                soa.albedo_b[idx] = 1.0;
                soa.fuzz[idx] = 0.0;
                soa.ior[idx] = 1.5;
            }
            idx += 1;
        }
    }

    for i in 0..NUM_SPHERES {
        soa.rsq[i] = soa.radius[i] * soa.radius[i];
    }
    for d in 124..128 {
        soa.cx[d] = 0.0;
        soa.cy[d] = -100000.0;
        soa.cz[d] = 0.0;
        soa.radius[d] = 0.0;
        soa.inv_radius[d] = 0.0;
        soa.rsq[d] = 0.0;
        soa.mat_type[d] = 0;
        soa.albedo_r[d] = 0.0;
        soa.albedo_g[d] = 0.0;
        soa.albedo_b[d] = 0.0;
        soa.fuzz[d] = 0.0;
        soa.ior[d] = 1.0;
    }

    let mut aos = Vec::with_capacity(NUM_SPHERES);
    for i in 0..NUM_SPHERES {
        aos.push(SphereAoS {
            center: Vec3::new(soa.cx[i], soa.cy[i], soa.cz[i]),
            radius: soa.radius[i],
            inv_radius: soa.inv_radius[i],
            albedo: Vec3::new(soa.albedo_r[i], soa.albedo_g[i], soa.albedo_b[i]),
            fuzz: soa.fuzz[i],
            ior: soa.ior[i],
            mat_type: soa.mat_type[i],
        });
    }

    (soa, aos)
}

// ----------------------------------------------------------------------------
// Intersection Kernels (Explicit AVX2 SIMD, SoA and AoS)
// ----------------------------------------------------------------------------
#[inline(always)]
unsafe fn hit_scene_simd(r: &Ray, t_min: f32, t_max: f32, rec: &mut HitRecord, s: &SphereSceneSoA) -> bool {
    use std::arch::x86_64::*;
    let mut hit_anything = false;
    let mut closest_so_far = t_max;
    let mut closest_idx = 0;

    let v_rox = _mm256_set1_ps(r.origin.x);
    let v_roy = _mm256_set1_ps(r.origin.y);
    let v_roz = _mm256_set1_ps(r.origin.z);
    let v_rdx = _mm256_set1_ps(r.dir.x);
    let v_rdy = _mm256_set1_ps(r.dir.y);
    let v_rdz = _mm256_set1_ps(r.dir.z);

    for b in 0..16 {
        let s_cx = _mm256_loadu_ps(s.cx.as_ptr().add(b * 8));
        let s_cy = _mm256_loadu_ps(s.cy.as_ptr().add(b * 8));
        let s_cz = _mm256_loadu_ps(s.cz.as_ptr().add(b * 8));
        let s_rsq = _mm256_loadu_ps(s.rsq.as_ptr().add(b * 8));

        let oc_x = _mm256_sub_ps(v_rox, s_cx);
        let oc_y = _mm256_sub_ps(v_roy, s_cy);
        let oc_z = _mm256_sub_ps(v_roz, s_cz);

        let half_b = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(oc_x, v_rdx), _mm256_mul_ps(oc_y, v_rdy)), _mm256_mul_ps(oc_z, v_rdz));
        let c = _mm256_sub_ps(_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(oc_x, oc_x), _mm256_mul_ps(oc_y, oc_y)), _mm256_mul_ps(oc_z, oc_z)), s_rsq);
        let disc = _mm256_sub_ps(_mm256_mul_ps(half_b, half_b), c);

        let cmp = _mm256_cmp_ps(disc, _mm256_setzero_ps(), _CMP_GT_OQ);
        let mask = _mm256_movemask_ps(cmp);
        if mask != 0 {
            let mut d = [0.0f32; 8];
            let mut hb = [0.0f32; 8];
            _mm256_storeu_ps(d.as_mut_ptr(), disc);
            _mm256_storeu_ps(hb.as_mut_ptr(), half_b);
            let base = b * 8;
            for k in 0..8 {
                let idx = base + k;
                if idx < 124 && d[k] > 0.0 {
                    let sqrtd = d[k].sqrt();
                    let r1 = -hb[k] - sqrtd;
                    let r2 = -hb[k] + sqrtd;
                    let root = if r1 <= t_min || closest_so_far <= r1 { r2 } else { r1 };
                    if root > t_min && root < closest_so_far {
                        closest_so_far = root;
                        closest_idx = idx;
                        hit_anything = true;
                    }
                }
            }
        }
    }

    if hit_anything {
        rec.t = closest_so_far;
        rec.p = r.origin.add(r.dir.mul_scalar(closest_so_far));
        let outward_normal = rec.p.sub(Vec3::new(s.cx[closest_idx], s.cy[closest_idx], s.cz[closest_idx])).mul_scalar(s.inv_radius[closest_idx]);
        rec.front_face = r.dir.dot(outward_normal) < 0.0;
        rec.normal = if rec.front_face { outward_normal } else { outward_normal.neg() };
        rec.mat_type = s.mat_type[closest_idx];
        rec.albedo = Vec3::new(s.albedo_r[closest_idx], s.albedo_g[closest_idx], s.albedo_b[closest_idx]);
        rec.fuzz = s.fuzz[closest_idx];
        rec.ior = s.ior[closest_idx];
    }
    hit_anything
}
#[inline(always)]
fn hit_scene_soa(r: &Ray, t_min: f32, t_max: f32, rec: &mut HitRecord, s: &SphereSceneSoA) -> bool {
    let mut hit_anything = false;
    let mut closest_so_far = t_max;
    let mut closest_idx = 0;

    let ro_x = r.origin.x;
    let ro_y = r.origin.y;
    let ro_z = r.origin.z;
    let rd_x = r.dir.x;
    let rd_y = r.dir.y;
    let rd_z = r.dir.z;

    for i in 0..NUM_SPHERES {
        let oc_x = ro_x - s.cx[i];
        let oc_y = ro_y - s.cy[i];
        let oc_z = ro_z - s.cz[i];
        let half_b = oc_x * rd_x + oc_y * rd_y + oc_z * rd_z;
        let c = oc_x * oc_x + oc_y * oc_y + oc_z * oc_z - s.radius[i] * s.radius[i];
        let discriminant = half_b * half_b - c;

        if discriminant > 0.0 {
            let sqrtd = discriminant.sqrt();
            let mut root = -half_b - sqrtd;
            if root <= t_min || closest_so_far <= root {
                root = -half_b + sqrtd;
            }
            if root > t_min && root < closest_so_far {
                closest_so_far = root;
                closest_idx = i;
                hit_anything = true;
            }
        }
    }

    if hit_anything {
        rec.t = closest_so_far;
        rec.p = r.origin.add(r.dir.mul_scalar(closest_so_far));
        let outward_normal = rec.p.sub(Vec3::new(s.cx[closest_idx], s.cy[closest_idx], s.cz[closest_idx])).mul_scalar(s.inv_radius[closest_idx]);
        rec.front_face = r.dir.dot(outward_normal) < 0.0;
        rec.normal = if rec.front_face { outward_normal } else { outward_normal.neg() };
        rec.mat_type = s.mat_type[closest_idx];
        rec.albedo = Vec3::new(s.albedo_r[closest_idx], s.albedo_g[closest_idx], s.albedo_b[closest_idx]);
        rec.fuzz = s.fuzz[closest_idx];
        rec.ior = s.ior[closest_idx];
    }
    hit_anything
}

#[inline(always)]
fn hit_scene_aos(r: &Ray, t_min: f32, t_max: f32, rec: &mut HitRecord, s: &[SphereAoS]) -> bool {
    let mut hit_anything = false;
    let mut closest_so_far = t_max;

    for sphere in s {
        let oc = r.origin.sub(sphere.center);
        let half_b = oc.dot(r.dir);
        let c = oc.dot(oc) - sphere.radius * sphere.radius;
        let discriminant = half_b * half_b - c;

        if discriminant > 0.0 {
            let sqrtd = discriminant.sqrt();
            let mut root = -half_b - sqrtd;
            if root <= t_min || closest_so_far <= root {
                root = -half_b + sqrtd;
            }
            if root > t_min && root < closest_so_far {
                closest_so_far = root;
                hit_anything = true;
                rec.t = root;
                rec.p = r.origin.add(r.dir.mul_scalar(root));
                let outward_normal = rec.p.sub(sphere.center).mul_scalar(sphere.inv_radius);
                rec.front_face = r.dir.dot(outward_normal) < 0.0;
                rec.normal = if rec.front_face { outward_normal } else { outward_normal.neg() };
                rec.mat_type = sphere.mat_type;
                rec.albedo = sphere.albedo;
                rec.fuzz = sphere.fuzz;
                rec.ior = sphere.ior;
            }
        }
    }
    hit_anything
}

// ----------------------------------------------------------------------------
// Camera & Ray Tracing Kernel
// ----------------------------------------------------------------------------
struct Camera {
    origin: Vec3,
    lower_left_corner: Vec3,
    horizontal: Vec3,
    vertical: Vec3,
}

fn init_camera() -> Camera {
    let origin = Vec3::new(13.0, 2.0, 3.0);
    let lookat = Vec3::new(0.0, 0.0, 0.0);
    let vup = Vec3::new(0.0, 1.0, 0.0);
    let vfov = 20.0f32;
    let aspect_ratio = WIDTH as f32 / HEIGHT as f32;

    let theta = vfov * (3.141592653589793 / 180.0);
    let h = (theta / 2.0).tan();
    let viewport_height = 2.0 * h;
    let viewport_width = aspect_ratio * viewport_height;

    let w = origin.sub(lookat).normalize();
    let u = vup.cross(w).normalize();
    let v = w.cross(u);

    let horizontal = u.mul_scalar(viewport_width);
    let vertical = v.mul_scalar(viewport_height);
    let lower_left_corner = origin
        .sub(horizontal.div_scalar(2.0))
        .sub(vertical.div_scalar(2.0))
        .sub(w);

    Camera { origin, lower_left_corner, horizontal, vertical }
}

#[inline(always)]
fn ray_color(r_in: Ray, rng: &mut u32, soa: &SphereSceneSoA, aos: &[SphereAoS], layout: Layout) -> Vec3 {
    let mut cur_attenuation = Vec3::new(1.0, 1.0, 1.0);
    let mut cur_ray = r_in;
    let mut final_color = Vec3::default();

    for _ in 0..MAX_DEPTH {
        let mut rec = HitRecord::default();
        let hit = match layout {
            Layout::Simd => unsafe { hit_scene_simd(&cur_ray, 0.001, 1e20, &mut rec, soa) },
            Layout::Soa => hit_scene_soa(&cur_ray, 0.001, 1e20, &mut rec, soa),
            Layout::Aos => hit_scene_aos(&cur_ray, 0.001, 1e20, &mut rec, aos),
        };

        if hit {
            if rec.mat_type == 0 {
                // Lambertian
                let mut scatter_dir = rec.normal.add(random_unit_vector(rng));
                if scatter_dir.x.abs() < 1e-6 && scatter_dir.y.abs() < 1e-6 && scatter_dir.z.abs() < 1e-6 {
                    scatter_dir = rec.normal;
                }
                cur_ray = Ray { origin: rec.p, dir: scatter_dir.normalize() };
                cur_attenuation = cur_attenuation.mul_vec(rec.albedo);
            } else if rec.mat_type == 1 {
                // Metal
                let reflected = reflect(cur_ray.dir.normalize(), rec.normal);
                let scatter_dir = reflected.add(random_unit_vector(rng).mul_scalar(rec.fuzz));
                cur_ray = Ray { origin: rec.p, dir: scatter_dir.normalize() };
                cur_attenuation = cur_attenuation.mul_vec(rec.albedo);
                if cur_ray.dir.dot(rec.normal) <= 0.0 {
                    cur_attenuation = Vec3::default();
                    break;
                }
            } else {
                // Dielectric (Glass)
                let refraction_ratio = if rec.front_face { 1.0 / rec.ior } else { rec.ior };
                let unit_dir = cur_ray.dir.normalize();
                let cos_theta = unit_dir.neg().dot(rec.normal).min(1.0);
                let sin_theta = (1.0 - cos_theta * cos_theta).max(0.0).sqrt();
                let cannot_refract = (refraction_ratio * sin_theta) > 1.0;

                let mut r0 = (1.0 - refraction_ratio) / (1.0 + refraction_ratio);
                r0 = r0 * r0;
                let p5 = 1.0 - cos_theta;
                let reflectance = r0 + (1.0 - r0) * (p5 * p5 * p5 * p5 * p5);

                let direction = if cannot_refract || reflectance > rand_float(rng) {
                    reflect(unit_dir, rec.normal)
                } else {
                    refract(unit_dir, rec.normal, refraction_ratio)
                };
                cur_ray = Ray { origin: rec.p, dir: direction.normalize() };
            }
        } else {
            // Background Sky
            let unit_dir = cur_ray.dir.normalize();
            let t = 0.5 * (unit_dir.y + 1.0);
            let sky = Vec3::new(1.0, 1.0, 1.0).mul_scalar(1.0 - t).add(Vec3::new(0.5, 0.7, 1.0).mul_scalar(t));
            final_color = cur_attenuation.mul_vec(sky);
            break;
        }
    }

    final_color
}

fn render_scanline(
    y: usize,
    frame_idx: usize,
    cam: &Camera,
    soa: &SphereSceneSoA,
    aos: &[SphereAoS],
    layout: Layout,
    row: &mut [u32],
) {
    let j = HEIGHT - 1 - y;
    for x in 0..WIDTH {
        let mut pixel_seed = (y as u32 * WIDTH as u32 + x as u32) * 1973 + frame_idx as u32 * 9277 + 1;
        xorshift32(&mut pixel_seed);
        xorshift32(&mut pixel_seed);

        let mut tot_r = 0.0f32;
        let mut tot_g = 0.0f32;
        let mut tot_b = 0.0f32;

        for _ in 0..SAMPLES_PER_PIXEL {
            let u_jitter = rand_float(&mut pixel_seed);
            let v_jitter = rand_float(&mut pixel_seed);
            let u_coord = (x as f32 + u_jitter) * (1.0 / WIDTH as f32);
            let v_coord = (j as f32 + v_jitter) * (1.0 / HEIGHT as f32);

            let ray_dir = cam.lower_left_corner
                .add(cam.horizontal.mul_scalar(u_coord))
                .add(cam.vertical.mul_scalar(v_coord))
                .sub(cam.origin);
            let r = Ray { origin: cam.origin, dir: ray_dir.normalize() };
            let c = ray_color(r, &mut pixel_seed, soa, aos, layout);
            tot_r += c.x;
            tot_g += c.y;
            tot_b += c.z;
        }

        let scale = 1.0 / SAMPLES_PER_PIXEL as f32;
        let mut r = tot_r * scale;
        let mut g = tot_g * scale;
        let mut b = tot_b * scale;

        r = r.clamp(0.0, 1.0).sqrt();
        g = g.clamp(0.0, 1.0).sqrt();
        b = b.clamp(0.0, 1.0).sqrt();

        let ir = (255.999 * r) as u32;
        let ig = (255.999 * g) as u32;
        let ib = (255.999 * b) as u32;
        let ia = 255u32;

        row[x] = ir | (ig << 8) | (ib << 16) | (ia << 24);
    }
}

fn save_bmp(path: &str, fb: &[u32]) -> bool {
    use std::io::Write;
    let mut file = match std::fs::File::create(path) {
        Ok(f) => f,
        Err(_) => return false,
    };

    let mut header = [0u8; 54];
    header[0] = b'B';
    header[1] = b'M';
    let file_size: u32 = 54 + (WIDTH * HEIGHT * 3) as u32;
    header[2..6].copy_from_slice(&file_size.to_le_bytes());
    header[10] = 54;
    header[14] = 40;
    let w: i32 = WIDTH as i32;
    let h: i32 = -(HEIGHT as i32);
    header[18..22].copy_from_slice(&w.to_le_bytes());
    header[22..26].copy_from_slice(&h.to_le_bytes());
    header[26] = 1;
    header[28] = 24;
    let img_sz: u32 = (WIDTH * HEIGHT * 3) as u32;
    header[34..38].copy_from_slice(&img_sz.to_le_bytes());
    let ppm: i32 = 2835;
    header[38..42].copy_from_slice(&ppm.to_le_bytes());
    header[42..46].copy_from_slice(&ppm.to_le_bytes());

    if file.write_all(&header).is_err() {
        return false;
    }

    let mut row = vec![0u8; WIDTH * 3];
    for y in 0..HEIGHT {
        for x in 0..WIDTH {
            let px = fb[y * WIDTH + x];
            row[x * 3 + 0] = ((px >> 16) & 0xFF) as u8;
            row[x * 3 + 1] = ((px >> 8) & 0xFF) as u8;
            row[x * 3 + 2] = (px & 0xFF) as u8;
        }
        if file.write_all(&row).is_err() {
            return false;
        }
    }
    true
}

fn compute_checksum(fb: &[u32]) -> u64 {
    let mut hash: u64 = 14695981039346656037;
    for &p in fb {
        let b0 = (p & 0xFF) as u8;
        let b1 = ((p >> 8) & 0xFF) as u8;
        let b2 = ((p >> 16) & 0xFF) as u8;
        let b3 = ((p >> 24) & 0xFF) as u8;
        hash = (hash ^ b0 as u64).wrapping_mul(1099511628211);
        hash = (hash ^ b1 as u64).wrapping_mul(1099511628211);
        hash = (hash ^ b2 as u64).wrapping_mul(1099511628211);
        hash = (hash ^ b3 as u64).wrapping_mul(1099511628211);
    }
    hash
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut headless = false;
    let mut headless_frames = 1;
    let mut layout = Layout::Soa;
    let mut take_screenshot = false;
    let mut gui_max_frames = 300;

    let mut i = 1;
    while i < args.len() {
        if args[i] == "--headless" && i + 1 < args.len() {
            headless = true;
            headless_frames = args[i + 1].parse().unwrap_or(1);
            i += 1;
        } else if args[i] == "--gui-frames" && i + 1 < args.len() {
            gui_max_frames = args[i + 1].parse().unwrap_or(300);
            i += 1;
        } else if args[i] == "--screenshot" {
            take_screenshot = true;
        } else if args[i] == "--aos" {
            layout = Layout::Aos;
        } else if args[i] == "--soa" {
            layout = Layout::Soa;
        } else if args[i] == "--simd" {
            layout = Layout::Simd;
        }
        i += 1;
    }

    let (soa, aos) = init_scene();
    let cam = init_camera();
    let mut fb = vec![0u32; WIDTH * HEIGHT];

    if headless {
        let layout_str = match layout {
            Layout::Simd => "SIMD",
            Layout::Soa => "SoA",
            Layout::Aos => "AoS",
        };
        println!("Running in HEADLESS mode ({} frames, layout={})...", headless_frames, layout_str);
        let mut total_ms = 0.0;
        let mut last_checksum = 0;

        for f in 0..headless_frames {
            let t0 = Instant::now();

            fb.par_chunks_exact_mut(WIDTH)
                .enumerate()
                .for_each(|(y, row)| {
                    render_scanline(y, f, &cam, &soa, &aos, layout, row);
                });

            let frame_ms = t0.elapsed().as_secs_f64() * 1000.0;
            total_ms += frame_ms;
            last_checksum = compute_checksum(&fb);

            if f == 0 && take_screenshot {
                save_bmp("benchmarks/results/raytracer_rust.bmp", &fb);
                println!("SCREENSHOT_SAVED: benchmarks/results/raytracer_rust.bmp");
            }

            let mrays = (WIDTH * HEIGHT * SAMPLES_PER_PIXEL) as f64 / (frame_ms * 1000.0);
            println!("FRAME {}: TIME_MS={:.2} MRAYS_PER_SEC={:.2} CHECKSUM=0x{:016X}", f, frame_ms, mrays, last_checksum);
        }

        let avg_ms = total_ms / headless_frames as f64;
        let avg_mrays = (WIDTH * HEIGHT * SAMPLES_PER_PIXEL) as f64 / (avg_ms * 1000.0);
        println!("BENCHMARK_COMPLETE: AVG_TIME_MS={:.2} AVG_MRAYS_PER_SEC={:.2} FINAL_CHECKSUM=0x{:016X}", avg_ms, avg_mrays, last_checksum);
        return;
    }

    // Interactive SDL3 Live Presentation Mode
    unsafe {
        if !SDL_Init(SDL_INIT_VIDEO) {
            let err = CStr::from_ptr(SDL_GetError()).to_string_lossy();
            eprintln!("Failed to initialize SDL3: {}", err);
            return;
        }

        let title = CString::new("Tamarin vs C++ vs Rust Raytracer (SDL3)").unwrap();
        let mut window: *mut SDL_Window = std::ptr::null_mut();
        let mut renderer: *mut SDL_Renderer = std::ptr::null_mut();

        if !SDL_CreateWindowAndRenderer(
            title.as_ptr(),
            WIDTH as c_int,
            HEIGHT as c_int,
            0,
            &mut window,
            &mut renderer,
        ) {
            let err = CStr::from_ptr(SDL_GetError()).to_string_lossy();
            eprintln!("Failed to create window/renderer: {}", err);
            SDL_Quit();
            return;
        }

        SDL_SetRenderVSync(renderer, 0); // Unthrottled

        let texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            WIDTH as c_int,
            HEIGHT as c_int,
        );

        if texture.is_null() {
            let err = CStr::from_ptr(SDL_GetError()).to_string_lossy();
            eprintln!("Failed to create texture: {}", err);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return;
        }

        println!("Starting unthrottled live raytracer (1280x720, 16 SPP, 124 spheres). Press ESC to exit, 'S' for screenshot.");

        let mut frame_idx = 0;
        let mut running = true;
        while running {
            let mut event = SDL_Event { type_: 0, padding: [0; 128] };
            while SDL_PollEvent(&mut event) {
                if event.type_ == SDL_EVENT_QUIT || event.type_ == SDL_EVENT_KEY_DOWN {
                    running = false;
                }
            }

            // 1. Strict isolated raytrace timing
            let t0 = Instant::now();
            fb.par_chunks_exact_mut(WIDTH)
                .enumerate()
                .for_each(|(y, row)| {
                    render_scanline(y, frame_idx, &cam, &soa, &aos, layout, row);
                });
            let trace_ms = t0.elapsed().as_secs_f64() * 1000.0;

            if frame_idx == 0 {
                save_bmp("benchmarks/results/raytracer_rust.bmp", &fb);
                println!("Saved screenshot to benchmarks/results/raytracer_rust.bmp");
            }

            // 2. Zero-copy SDL3 texture streaming
            let mut pixels: *mut c_void = std::ptr::null_mut();
            let mut pitch: c_int = 0;
            if SDL_LockTexture(texture, std::ptr::null(), &mut pixels, &mut pitch) {
                let pitch = pitch as usize;
                for y in 0..HEIGHT {
                    let src = &fb[y * WIDTH..(y + 1) * WIDTH];
                    let dst = (pixels as *mut u8).add(y * pitch) as *mut u32;
                    std::ptr::copy_nonoverlapping(src.as_ptr(), dst, WIDTH);
                }
                SDL_UnlockTexture(texture);
            }

            SDL_RenderTexture(renderer, texture, std::ptr::null(), std::ptr::null());
            SDL_RenderPresent(renderer);

            let mrays = (WIDTH * HEIGHT * SAMPLES_PER_PIXEL) as f64 / (trace_ms * 1000.0);
            print!("\rFrame {} | Trace: {:.2} ms ({:.2} Mrays/s) | Present: OK", frame_idx, trace_ms, mrays);

            frame_idx += 1;
            if frame_idx >= gui_max_frames { break; }
        }
        println!();

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
}
