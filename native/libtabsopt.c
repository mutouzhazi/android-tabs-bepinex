/* TabsOpt v9 (intl)
 * - throttle ECS decision systems (every 3rd frame)
 * - physics: solver 2, sleepThreshold 0.03, fixedDeltaTime 1/30, engine limits
 * - FPS reporter
 * - FusionCore bootstrap: stage fusion config, then boot BepInEx (CoreCLR host)
 */
typedef unsigned long usize;
typedef long isize;
typedef unsigned int u32;
typedef unsigned char u8;

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define RTLD_NOLOAD 4
#define THROTTLE 3

typedef struct { void *jobGroup; int version; int pad; } JobHandle;
typedef struct { long tv_sec; long tv_nsec; } Timespec;

extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *sym);
extern int mprotect(void *addr, usize len, int prot);
extern void *mmap(void *addr, usize len, int prot, int flags, int fd, isize off);
extern long syscall(long num, ...);
extern int ftruncate(int fd, isize len);
extern int open(const char *path, int flags, int mode);
extern long write(int fd, const void *buf, usize n);
extern int usleep(unsigned usec);
extern int clock_gettime(int clk, Timespec *ts);
extern int pthread_create(unsigned long *tid, const void *attr, void *(*fn)(void *), void *arg);
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);

#define LOGI(...) __android_log_print(4, "TabsOpt", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "TabsOpt", __VA_ARGS__)

static int g_logfd = -1;
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int scontains(const char *h, const char *n) {
    int hl = slen(h), nl = slen(n);
    if (!nl || hl < nl) return 0;
    for (int i = 0; i + nl <= hl; i++) {
        int ok = 1;
        for (int j = 0; j < nl; j++) if (h[i + j] != n[j]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}
static int seq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == 0 && b[i] == 0;
}

static void logfile(const char *s) {
    if (g_logfd >= 0) { write(g_logfd, s, slen(s)); write(g_logfd, "\n", 1); }
}
static long long now_ms(void) {
    Timespec ts;
    if (clock_gettime(1, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* il2cpp api */
static void *(*p_domain_get)(void);
static void *(*p_domain_get_assemblies)(void *, usize *);
static void *(*p_assembly_get_image)(void *);
static void *(*p_class_from_name)(void *, const char *, const char *);
static void *(*p_class_get_method_from_name)(void *, const char *, int);
static void *(*p_runtime_invoke)(void *, void *, void **, void **);
static u32 (*p_image_get_class_count)(void *);
static void *(*p_image_get_class)(void *, u32);
static const char *(*p_class_get_name)(void *);
static const char *(*p_class_get_namespace)(void *);
static void *(*p_class_get_methods)(void *, void **);
static const char *(*p_method_get_name)(void *);
static u32 (*p_method_get_param_count)(void *);

static void resolve_api(void *h) {
    p_domain_get = dlsym(h, "il2cpp_domain_get");
    p_domain_get_assemblies = dlsym(h, "il2cpp_domain_get_assemblies");
    p_assembly_get_image = dlsym(h, "il2cpp_assembly_get_image");
    p_class_from_name = dlsym(h, "il2cpp_class_from_name");
    p_class_get_method_from_name = dlsym(h, "il2cpp_class_get_method_from_name");
    p_runtime_invoke = dlsym(h, "il2cpp_runtime_invoke");
    p_image_get_class_count = dlsym(h, "il2cpp_image_get_class_count");
    p_image_get_class = dlsym(h, "il2cpp_image_get_class");
    p_class_get_name = dlsym(h, "il2cpp_class_get_name");
    p_class_get_namespace = dlsym(h, "il2cpp_class_get_namespace");
    p_class_get_methods = dlsym(h, "il2cpp_class_get_methods");
    p_method_get_name = dlsym(h, "il2cpp_method_get_name");
    p_method_get_param_count = dlsym(h, "il2cpp_method_get_param_count");
}

static void *find_image(const char *part) {
    if (!p_domain_get) return 0;
    void *dom = p_domain_get();
    if (!dom) return 0;
    usize n = 0;
    void **asms = p_domain_get_assemblies(dom, &n);
    if (!asms) return 0;
    for (usize i = 0; i < n; i++) {
        void *img = p_assembly_get_image(asms[i]);
        void *c = p_class_from_name(img, "UnityEngine", part);
        if (c) return img;
    }
    return 0;
}

static void *find_class(const char *ns, const char *name) {
    if (!p_domain_get) return 0;
    void *dom = p_domain_get();
    if (!dom) return 0;
    usize n = 0;
    void **asms = p_domain_get_assemblies(dom, &n);
    if (!asms) return 0;
    for (usize i = 0; i < n; i++) {
        void *img = p_assembly_get_image(asms[i]);
        void *c = p_class_from_name(img, ns, name);
        if (c) return c;
    }
    return 0;
}

static void flush_icache(void *p, usize n) {
    unsigned long a = (unsigned long)p, e = a + n;
    for (unsigned long x = a; x < e; x += 64) __asm__ __volatile__("dc cvau, %0" :: "r"(x) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    for (unsigned long x = a; x < e; x += 64) __asm__ __volatile__("ic ivau, %0" :: "r"(x) : "memory");
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

static void *patch_jump(void *target, void *hook) {
    u8 *t = (u8 *)target;
    unsigned long page = (unsigned long)t & ~0xFFFUL;
    int mp = mprotect((void *)page, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (mp != 0) { LOGE("mprotect RWX failed rc=%d", mp); return 0; }
    int fd = (int)syscall(279, "tabsopt", 0);
    if (fd < 0) return 0;
    if (ftruncate(fd, 4096) != 0) return 0;
    u8 *rw = (u8 *)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    u8 *rx = (u8 *)mmap(0, 4096, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    if (rw == (void *)-1 || rx == (void *)-1) return 0;
    for (int i = 0; i < 16; i++) rw[i] = t[i];
    u32 *ins = (u32 *)(rw + 16);
    ins[0] = 0x58000050u;
    ins[1] = 0xD61F0200u;
    *(void **)(rw + 24) = (void *)(t + 16);
    u32 *ti = (u32 *)t;
    ti[0] = 0x58000050u;
    ti[1] = 0xD61F0200u;
    *(void **)(t + 8) = hook;
    mprotect((void *)page, 0x1000, PROT_READ | PROT_EXEC);
    flush_icache(t, 16);
    flush_icache(rw, 32);
    return rx;
}

/* ---------------- physics ---------------- */
static int g_phys_done;

static void *find_class_mod(const char *cls, const char *fallback) {
    void *img = find_image(cls);
    if (!img && fallback) img = find_image(fallback);
    return img;
}

static int set_int_static(const char *cls, const char *setter, int v) {
    void *img = find_class_mod(cls, "PhysicsModule");
    if (!img) { img = find_image("Physics"); }
    if (!img) return -1;
    void *c = p_class_from_name(img, "UnityEngine", cls);
    if (!c) return -2;
    void *m = p_class_get_method_from_name(c, setter, 1);
    if (!m) return -3;
    void *exc = 0;
    void *args[1] = { &v };
    p_runtime_invoke(m, 0, args, &exc);
    return exc ? -4 : 0;
}

static int set_float_static(const char *cls, const char *setter, float v) {
    void *img = find_class_mod(cls, "PhysicsModule");
    if (!img) { img = find_image("Physics"); }
    if (!img) return -1;
    void *c = p_class_from_name(img, "UnityEngine", cls);
    if (!c) return -2;
    void *m = p_class_get_method_from_name(c, setter, 1);
    if (!m) return -3;
    void *exc = 0;
    void *args[1] = { &v };
    p_runtime_invoke(m, 0, args, &exc);
    return exc ? -4 : 0;
}

static int set_any(const char *cls, const char *setter, void *val) {
    void *c = find_class("UnityEngine", cls);
    if (!c) return -2;
    void *m = p_class_get_method_from_name(c, setter, 1);
    if (!m) return -3;
    void *exc = 0;
    void *args[1] = { val };
    p_runtime_invoke(m, 0, args, &exc);
    return exc ? -4 : 0;
}
static void apply_physics(void) {
    if (g_phys_done) return;
    g_phys_done = 1;
    int a = set_int_static("Physics", "set_defaultSolverIterations", 2);
    int b = set_float_static("Physics", "set_sleepThreshold", 0.03f);
    int c = set_float_static("Time", "set_fixedDeltaTime", 0.03333f);
    int tfr = 60, vs = 0;
    float mdt = 0.1f;
    unsigned char bf = 0;
    int d = set_any("Application", "set_targetFrameRate", &tfr);
    int e = set_any("QualitySettings", "set_vSyncCount", &vs);
    int f = set_any("Time", "set_maximumDeltaTime", &mdt);
    int g = set_any("Physics", "set_autoSyncTransforms", &bf);
    LOGI("physics solver=%d sleep=%d fixed=%d", a, b, c);
    LOGI("engine fps=%d vsync=%d maxdt=%d autosync=%d", d, e, f, g);
    logfile("physics applied");
}

/* ---------------- systems ---------------- */
typedef struct { const char *ns; const char *name; } SysDef;

static SysDef JOBS[9] = {
    { "Landfall.TABS.AI.Systems", "FindEnemyTargetSystem" },
    { "Landfall.TABS.AI.Systems", "FindFriendlyTargetSystem" },
    { "Landfall.TABS.AI.Systems", "FindFriendlyHighestPriceTargetSystem" },
    { "Landfall.TABS.AI.Systems", "FindNearestFriendTargetSystem" },
    { "Landfall.TABS.AI.Systems", "CanSeeSystem" },
    { "Landfall.TABS.AI.Systems", "TargetSystem" },
    { "Landfall.TABS.AI.Systems.Modifiers", "FleeWithinDistanceSystem" },
    { "Landfall.TABS.AI.Systems.Modifiers", "KeepPreferredDistanceSystem" },
    { "Landfall.TABS.AI.Systems.Modifiers", "KeepRangedDistanceSystem" },
};
static void *g_orig_job[9];
static int g_ctr_job[9];

static long long g_fps_t0;
static int g_fps_frames;

#define DECL_JOB(I) \
    static JobHandle hj##I(void *self, JobHandle in) { \
        if ((g_ctr_job[I]++ % THROTTLE) != 0) return in; \
        return ((JobHandle (*)(void *, JobHandle))g_orig_job[I])(self, in); \
    }
DECL_JOB(0) DECL_JOB(1) DECL_JOB(2) DECL_JOB(3) DECL_JOB(4)
DECL_JOB(5) DECL_JOB(6) DECL_JOB(7) DECL_JOB(8)

static void *JOB_HOOKS[9] = { (void *)hj0, (void *)hj1, (void *)hj2, (void *)hj3, (void *)hj4,
                              (void *)hj5, (void *)hj6, (void *)hj7, (void *)hj8 };

static JobHandle hj0_main(void *self, JobHandle in) {
    apply_physics();
    g_fps_frames++;
    if (g_fps_frames >= 300) {
        long long t = now_ms();
        if (g_fps_t0) LOGI("FPS ~ %.1f", 300000.0 / (double)(t - g_fps_t0));
        g_fps_t0 = t;
        g_fps_frames = 0;
    }
    if ((g_ctr_job[0]++ % THROTTLE) != 0) return in;
    return ((JobHandle (*)(void *, JobHandle))g_orig_job[0])(self, in);
}

static void (*g_orig_nav)(void *self);
static int g_ctr_nav;
static void h_nav(void *self) {
    if ((g_ctr_nav++ % THROTTLE) != 0) return;
    g_orig_nav(self);
}

/* ---------------- fusion bootstrap ---------------- */
#define FILES_DIR "/storage/emulated/0/Android/data/com.xd.tabs.google/files"
#define UNITY_VER "2022.3.62f3c1"

typedef struct {
    void *r0, *r1, *r2;
    int (*DestroyJavaVM)(void *);
    int (*AttachCurrentThread)(void *, void **, void *);
    int (*DetachCurrentThread)(void *);
    int (*GetEnv)(void *, void **, int);
} JNIInvokeIF;

static char g_libdir[256];

extern char *dlerror(void);
static int app_str(char *o, int off, const char *s);

/* preload siblings by absolute path (no RUNPATH in libfusion), then open libfusion */
static void *open_fusion(void) {
    static const char *PRE[] = {
        "/libc++_shared.so", "/libcoreclr.so", "/libclrjit.so",
        "/libmscordaccore.so", "/libmscordbi.so",
        "/libSystem.Native.so", "/libSystem.Globalization.Native.so",
        "/libSystem.IO.Compression.Native.so",
        "/libSystem.Security.Cryptography.Native.OpenSsl.so",
        "/libcrypto.so", "/libssl.so"
    };
    char p[600];
    for (int i = 0; i < 11; i++) {
        int o = 0;
        o = app_str(p, o, g_libdir); o = app_str(p, o, PRE[i]); p[o] = 0;
        void *h = dlopen(p, 2 | 0x100);
        if (!h) LOGI("preload %s fail: %s", PRE[i], dlerror());
    }
    int o = 0;
    o = app_str(p, o, g_libdir); o = app_str(p, o, "/libfusion.so"); p[o] = 0;
    void *h = dlopen(p, 2);
    if (!h) { LOGE("dlopen %s fail: %s", p, dlerror()); logfile("libfusion open failed"); }
    else LOGI("fusion opened: %s", p);
    return h;
}

static int app_str(char *o, int off, const char *s) {
    while (*s) o[off++] = *s++;
    return off;
}

/* ---- libmain override API that libfusion expects ---- */
static char g_ov_il2cpp[512];
static char g_ov_unity[512];

static void copy_str(char *d, int cap, const char *s) {
    int i = 0;
    if (s) { while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } }
    d[i] = 0;
}
void libmain_set_override_il2cpp_path(const char *p) {
    copy_str(g_ov_il2cpp, 512, p);
    LOGI("override il2cpp path: %s", g_ov_il2cpp);
}
const char *libmain_get_override_il2cpp_path(void) {
    return g_ov_il2cpp[0] ? g_ov_il2cpp : 0;
}
void libmain_set_override_unity_path(const char *p) {
    copy_str(g_ov_unity, 512, p);
    LOGI("override unity path: %s", g_ov_unity);
}
const char *libmain_get_override_unity_path(void) {
    return g_ov_unity[0] ? g_ov_unity : 0;
}

static int read_libdir(void) {
    int fd = open("/proc/self/maps", 0, 0);
    if (fd < 0) { LOGE("maps open failed"); return 0; }
    static char buf[131072];
    long n = 0, r;
    while (n < (long)sizeof(buf) - 1) {
        r = (long)syscall(63, fd, buf + n, (usize)(sizeof(buf) - 1 - n));
        if (r <= 0) break;
        n += r;
    }
    syscall(57, fd);
    buf[n] = 0;
    long start = 0;
    for (long i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
            buf[i] = 0;
            const char *line = buf + start;
            if (scontains(line, "libtabsopt.so") || scontains(line, "libmain.so")) {
                const char *p = line;
                while (*p && *p != '/') p++;
                const char *slash = 0;
                for (const char *q = p; *q; q++) if (*q == '/') slash = q;
                if (slash && slash > p) {
                    int len = (int)(slash - p);
                    if (len > 240) len = 240;
                    for (int k = 0; k < len; k++) g_libdir[k] = p[k];
                    g_libdir[len] = 0;
                    return 1;
                }
            }
            start = i + 1;
        }
    }
    return 0;
}

static int write_fusion_cfg(void) {
    char b[2048];
    char path[512];
    int o = 0;
    o = app_str(b, o, "gameLibraryDirectory="); o = app_str(b, o, g_libdir); o = app_str(b, o, "\n");
    o = app_str(b, o, "appLibraryDirectory="); o = app_str(b, o, g_libdir); o = app_str(b, o, "\n");
    o = app_str(b, o, "appDataDirectory=" FILES_DIR "\n");
    o = app_str(b, o, "bepInExDirectory=" FILES_DIR "/BepInEx\n");
    o = app_str(b, o, "dotnetDirectory=" FILES_DIR "/dotnet\n");
    o = app_str(b, o, "unityDataDirectory=" FILES_DIR "/unitydata\n");
    o = app_str(b, o, "unityVersion=" UNITY_VER "\n");
    o = app_str(b, o, "useOriginalLibUnity=true\n");
    b[o] = 0;
    int blen = o;

    o = 0;
    o = app_str(path, o, FILES_DIR "/fusion.cfg");
    path[o] = 0;

    int fd = open(path, 0x241, 420);
    if (fd < 0) { LOGE("cfg open failed: %s", path); return -1; }
    long w = write(fd, b, (usize)blen);
    syscall(57, fd);
    LOGI("cfg written %ld bytes", w);
    return (w > 0) ? 0 : -2;
}

static void *fusion_worker(void *arg) {
    (void)arg;
    int (*getvms)(void **, int, int *) = (int (*)(void **, int, int *))dlsym(0, "JNI_GetCreatedJavaVMs");
    if (!getvms) {
        void *nh = dlopen("libnativehelper.so", RTLD_NOLOAD);
        if (!nh) nh = dlopen("libart.so", RTLD_NOLOAD);
        if (nh) getvms = (int (*)(void **, int, int *))dlsym(nh, "JNI_GetCreatedJavaVMs");
    }
    LOGI("getvms=%p", getvms);
    void *vm = 0;
    int cnt = 0;
    if (getvms) {
        for (int i = 0; i < 500; i++) {
            vm = 0; cnt = 0;
            if (getvms(&vm, 1, &cnt) == 0 && vm && cnt > 0) break;
            vm = 0;
            usleep(20000);
        }
    }
    LOGI("vm=%p cnt=%d", vm, cnt);
    void *env = 0;
    if (vm) {
        JNIInvokeIF *f = *(JNIInvokeIF **)vm;
        if (f && f->AttachCurrentThread) {
            int rc = f->AttachCurrentThread(vm, &env, 0);
            LOGI("attach rc=%d env=%p", rc, env);
        }
    }
    void *hf = open_fusion();
    if (!hf) { LOGE("open_fusion failed in worker"); return 0; }
    int (*boot)(void *) = (int (*)(void *))dlsym(hf, "fusion_bootstrap_from_libmain");
    LOGI("boot=%p", boot);
    if (boot) {
        int r = boot(env);
        LOGI("boot rc=%d", r);
        logfile("fusion boot called");
    } else { LOGE("boot symbol missing"); logfile("boot symbol missing"); }
    return 0;
}

static void fusion_stage_sync(void) {
    if (!read_libdir()) { LOGE("libdir not found"); logfile("libdir not found"); return; }
    LOGI("libdir=%s", g_libdir);
    if (write_fusion_cfg() != 0) { return; }
    void *h = open_fusion();
    if (!h) { LOGE("open_fusion (stage) failed"); logfile("libfusion stage open failed"); return; }
    int (*stage)(const char *) = (int (*)(const char *))dlsym(h, "fusion_stage_from_config_path");
    LOGI("stage=%p", stage);
    if (stage) {
        int r = stage(FILES_DIR "/fusion.cfg");
        LOGI("stage rc=%d", r);
        logfile("fusion stage done");
    } else { LOGE("stage symbol missing"); logfile("stage symbol missing"); }
}

static void *worker(void *arg) {
    (void)arg;
    void *h = 0;
    for (int i = 0; i < 6000 && !h; i++) {
        h = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (!h) usleep(20000);
    }
    if (!h) { LOGE("libil2cpp never loaded"); return 0; }
    LOGI("libil2cpp loaded, v9");
    usleep(2000000);
    resolve_api(h);

    for (int i = 0; i < 9; i++) {
        void *c = 0;
        for (int k = 0; k < 40 && !c; k++) {
            c = find_class(JOBS[i].ns, JOBS[i].name);
            if (!c) usleep(100000);
        }
        if (!c) { LOGE("%s NOT FOUND", JOBS[i].name); continue; }
        void *mi = p_class_get_method_from_name(c, "OnUpdate", 1);
        if (!mi) { LOGE("%s.OnUpdate(1) NOT FOUND", JOBS[i].name); continue; }
        void *tramp = patch_jump(*(void **)mi, i == 0 ? (void *)hj0_main : JOB_HOOKS[i]);
        if (!tramp) { LOGE("%s patch failed", JOBS[i].name); continue; }
        g_orig_job[i] = tramp;
        LOGI("%s hooked", JOBS[i].name);
        logfile(JOBS[i].name);
    }

    {
        void *c = 0;
        for (int k = 0; k < 40 && !c; k++) {
            c = find_class("Landfall.TABS.AI.Systems", "CalculateNavPathSystem");
            if (!c) usleep(100000);
        }
        if (c) {
            void *mi = p_class_get_method_from_name(c, "OnUpdate", 0);
            if (mi) {
                void *tramp = patch_jump(*(void **)mi, (void *)h_nav);
                if (tramp) { g_orig_nav = (void (*)(void *))tramp; LOGI("CalculateNavPathSystem hooked"); }
                else LOGE("CalculateNavPathSystem patch failed");
            } else LOGE("CalculateNavPathSystem.OnUpdate(0) NOT FOUND");
        } else LOGE("CalculateNavPathSystem NOT FOUND");
    }

    LOGI("v9 worker done");
    logfile("v9 worker done");
    return 0;
}

__attribute__((constructor)) static void tabsopt_main(void) {
    g_logfd = open(FILES_DIR "/tabsopt.log", 0x441, 420);
    LOGI("intl v9 loaded");
    logfile("intl v9 loaded");
    fusion_stage_sync();
    unsigned long tid = 0;
    pthread_create(&tid, 0, fusion_worker, 0);
    pthread_create(&tid, 0, worker, 0);
}