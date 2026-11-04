# GLB Viewer (C + OpenGL)

Viewer minimal untuk file `.glb` / `.gltf` yang ditulis murni dalam bahasa C,
dengan arsitektur modular: lifecycle aplikasi, kamera, pipeline GL, dan
renderer dipisah menjadi modul-modul kecil yang dapat dipakai ulang.

## Fitur

- **PBR metallic-roughness lengkap** (Cook–Torrance: GGX + Smith + Schlick)
- **IBL berbasis cubemap**: environment → cubemap → irradiance (diffuse),
  prefiltered specular (5 mip), BRDF LUT (split-sum Karis)
- Environment: file `.hdr` (equirectangular) atau langit prosedural
- Tekstur glTF: albedo (sRGB), metallic-roughness, normal, occlusion, emissive
- Alpha mode OPAQUE / MASK / BLEND (dua pass render)
- Tone-mapping ACES + gamma correction
- Kamera orbit (drag kiri = rotate, scroll = zoom) + auto-fit bounding sphere
- Parsing GLB / glTF (embedded maupun eksternal) via `cgltf`
- Tekstur (PNG/JPG) via `stb_image`
- Custom OpenGL loader (tanpa GLEW/GLAD) — resolusi pointer via GLFW
- FPS counter di judul window
- Logging terpadu via `core/log.h` (semua ke stderr)

## Dependensi (semua otomatis di-download oleh CMake)

- GLFW 3.4
- cglm v0.9.4
- cgltf (single header)
- stb_image (single header)

## Arsitektur

Struktur direktori mengikuti prinsip **by-concern**: tiap modul hanya
bergantung pada modul di bawahnya, tidak ada dependency sirkular.

```
.
├── CMakeLists.txt
├── README.md
├── .gitignore
├── external/               # third-party single-header
│   ├── stb_image.h         # opsional — diunduh CMake bila tidak ada
│   ├── stb_impl.c
│   └── cgltf.h             # opsional — diunduh CMake bila tidak ada
└── src/
    ├── main.c              # bootstrap ~10 baris (create → run → destroy)
    ├── app/                # lifecycle window, render loop, input
    │   ├── app.h
    │   └── app.c
    ├── camera/             # OrbitCamera + math view/proj (bebas GL/GLFW)
    │   ├── camera.h
    │   └── camera.c
    ├── core/               # logging
    │   ├── log.h
    │   └── log.c
    ├── gl/                 # loader OpenGL prosedural
    │   ├── gl_loader.h
    │   └── gl_loader.c
    ├── gfx/                # shader builder + IBL bake pipeline
    │   ├── shader.h
    │   ├── shader.c
    │   ├── pbr_ibl.h
    │   └── pbr_ibl.c
    └── model/              # parser glTF → GPU buffers
        ├── gltf_model.h
        └── gltf_model.c
```

### Tanggung jawab modul

| Modul     | Tanggung jawab                                                   | Dependensi           |
| --------- | ---------------------------------------------------------------- | -------------------- |
| `app/`    | Lifecycle window, render loop, uniform material, input callbacks | semua modul di bawah |
| `camera/` | State kamera + math proyeksi/view                                | cglm (saja)          |
| `core/`   | Logging terpadu                                                  | libc                 |
| `gl/`     | Loader fungsi OpenGL + konstanta                                 | libc                 |
| `gfx/`    | Shader builder + IBL bake pipeline                               | `gl/`, `core/`       |
| `model/`  | Parser glTF → VAO/VBO/tekstur                                    | `gl/`, `core/`       |
| `main.c`  | Bootstrap                                                        | `app/`               |

Kamera **tidak** meng-include GLFW atau OpenGL — murni cglm — sehingga bisa
dipakai ulang di headless viewer atau unit test.

## Build

### Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Hasil: `build/glb_viewer`

### Windows (MinGW + Ninja)

```bat
cmake -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
```

Hasil: `build-ninja\glb_viewer.exe`

> **Prasyarat Ninja di Windows:** `ninja.exe` harus ada di `PATH`. Bisa dari
> instalasi Visual Studio (Developer PowerShell for VS 2022) atau
> `winget install Ninja-build.Ninja`. Kalau dijalankan dari PowerShell biasa
> dan compiler belum ada di `PATH`, CMake akan gagal dengan pesan
> _"No CMAKE_C_COMPILER could be found."_. Solusinya: jalankan dari
> **Developer PowerShell for VS 2022**, atau pakai generator Visual Studio.

### Windows (MSVC / Visual Studio 2022)

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

Hasil: `build-msvc\Release\glb_viewer.exe`

> Generator Visual Studio bersifat _multi-config_ sehingga executable berada
> di subfolder `Release\` (atau `Debug\`), bukan langsung di folder build.

### Pakai dependency sistem (opsional)

Kalau `glfw3` dan `cglm` sudah terinstall:

```bash
cmake -B build -DUSE_SYSTEM_DEPS=ON
cmake --build build -j
```

### Kalau muncul error generator mismatch

```
CMake Error: generator "Visual Studio 17 2022"
Does not match the generator used previously: Ninja
```

Artinya folder `build/` sudah pernah dikonfigurasi dengan generator berbeda.
CMake mengunci generator di `CMakeCache.txt` dan tidak mau menggantinya.
Pilih salah satu:

1. Hapus folder build lama lalu konfigurasi ulang:
   ```powershell
   Remove-Item -Recurse -Force build
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   ```
2. Pakai folder build baru (mis. `build-msvc`) — cara paling aman kalau
   ingin menyimpan build Ninja yang sudah ada.
3. Kembali ke generator lama (Ninja) dan lanjutkan seperti sebelumnya.

**Saran:** biasakan pakai nama folder berbeda per generator —
`build-ninja`, `build-msvc`, `build-debug` — agar error ini tidak terulang.

## Menjalankan

```
<build-dir>/glb_viewer <model.glb> [env.hdr]
```

Contoh:

```bash
# Linux / macOS
./build/glb_viewer path/to/model.glb
./build/glb_viewer path/to/model.glb assets/studio.hdr

# Windows (Ninja)
build-ninja\glb_viewer.exe path\to\model.glb

# Windows (MSVC)
build-msvc\Release\glb_viewer.exe path\to\model.glb
```

- Argumen pertama (wajib): file `.glb` / `.gltf`.
- Argumen kedua (opsional): file HDR equirectangular untuk IBL.
  Tanpa argumen ini, viewer memakai langit prosedural.

## Kontrol

| Aksi          | Input     |
| ------------- | --------- |
| Rotasi kamera | Drag kiri |
| Zoom          | Scroll    |
| Keluar        | Esc       |

## Catatan teknis

- `gl_loader.h` mendefinisikan ulang tipe & fungsi GL sendiri supaya tidak
  bergantung pada header `<GL/gl.h>` sistem. Resolusi pointer fungsi tetap
  memakai `glfwGetProcAddress` (GLFW), bukan GLEW/GLAD.
- `src/app/app.c` mendefinisikan `GLFW_INCLUDE_NONE` sebelum
  `<GLFW/glfw3.h>` agar GLFW tidak menarik header GL sistem.
- Target context: OpenGL 4.1 core (fallback ke 3.3). IBL butuh render
  target float (`RGBA16F`); jika tidak tersedia, viewer memberi peringatan
  di `stderr` dan turun ke `RGBA8` (kualitas IBL menurun).
- Semua log lewat `core/log.h` → **stderr** (selalu terlihat meski stdout
  di-redirect).
- GL state disimpan di `struct AppState` di `app.c` (bukan variabel global).
  Callback GLFW memakai `glfwGetWindowUserPointer`, sehingga viewer ini siap
  untuk multi-window atau di-refactor jadi library.
- Kamera state + math sepenuhnya bebas GL/GLFW (`camera/camera.c` hanya
  meng-include `cglm` dan `math.h`), memudahkan unit test.
- `CMakeLists.txt` mengekspor `compile_commands.json` ke folder build —
  bisa langsung dipakai clangd / IDE.

## Build cepat (ringkasan)

```bash
# 1. Konfigurasi + build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 2. Jalankan (Linux / macOS)
./build/glb_viewer model.glb

# 2b. Jalankan (Windows, Ninja)
build-ninja\glb_viewer.exe model.glb
```

Pada build pertama, CMake akan mengunduh GLFW, cglm, stb, dan cgltf
via `FetchContent` (±1–2 menit tergantung jaringan). Build berikutnya
memakai cache di `build/_deps`.

## Roadmap (opsional)

Fitur yang mudah ditambahkan di atas arsitektur ini:

- Toggle wireframe (butuh tambahan `glPolygonMode` di `gl_loader.h`)
- Screenshot via `stbi_write_png` + `glReadPixels`
- Auto-rotate kamera saat idle
- Slider exposure (`+`/`-`)
- Background gradient statis saat IBL gagal dibake
- Hot-reload shader (file `.glsl` terpisah + watcher)

---
