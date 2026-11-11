# NOTAS — Marca Lagoonic y ajustes por defecto (motor)

> Agente: marca propia del fork. Ambito EXCLUSIVO de este NOTAS:
> `Engine/godot/version.py` (unico editor autorizado) + ficheros NUEVOS en
> `Engine/godot/lagoonic_branding/`. No se toco nada mas. Sin commit.

## 0. Estado del clon (verificacion previa)

- `Engine/godot/` ya estaba clonado y completo: `git describe --tags` =
  `4.7.2-stable`, HEAD = `ed1daf0` ("Bump version to 4.7.2-stable"),
  remoto `https://github.com/godotengine/godot.git`, arbol limpio.
- No hizo falta esperar ni clonar nada (prohibido clonar otro).
- Otros agentes ya trabajan en este mismo clon (`editor/lagoonic/` y
  `modules/lagoonic/` aparecen como untracked): no se tocan.

## 1. `../version.py` (EDITADO — unico fichero upstream tocado)

Diff exacto (2 lineas, sin comentarios ni lineas en blanco nuevos: el parser
de `platform/android/java/app/config.gradle` hace `line.split("=")` y
revienta con lineas sin `=`):

```diff
-short_name = "godot"          (SIN CAMBIO)
-name = "Godot Engine"         ->  name = "Lagoonic Engine"
-major = 4 / minor = 7 / patch = 2 / status = "stable" / docs = "4.7"
-                                 (SIN CAMBIO)
-module_config = ""            ->  module_config = ".lagoonic.1"
-website = "https://godotengine.org"   (SIN CAMBIO)
+short_name = "godot"
+name = "Lagoonic Engine"
+major = 4
+minor = 7
+patch = 2
+status = "stable"
+module_config = ".lagoonic.1"
+website = "https://godotengine.org"
+docs = "4.7"
```

Por que asi (compatibilidad):

- `short_name = "godot"` SE MANTIENE: lo usan `OS::get_godot_dir_name()`
  (carpetas de usuario), el nombre de app en macOS/Windows y la deteccion
  `is_engine()` de `methods.py` (busca literalmente `short_name = "godot"`).
  Cambiarlo romperia rutas, builds y el asset library.
- `major/minor/patch/status/docs` SE MANTIENEN (`4.7.2-stable`, docs `4.7`):
  de ellos dependen `project.godot`/`config/features`, `editor_settings-4.7.tres`,
  plantillas de exportacion `4.7.2.stable`, `Engine.get_version_info()` en
  GDScript (solo expone major/minor/patch/status/build: QUEDA IDENTICO) y las
  comprobaciones `status == "stable"` (`editor_settings.cpp`,
  `engine_update_label.cpp`, `build_assemblies.py`). Cambiar `status` a
  `stable-lagoonic.1` habria puesto los update-checks en modo "Preview".
- `name = "Lagoonic Engine"`: solo afecta a cadenas visibles (titulo de
  ventana `... - Lagoonic Engine` en `editor_node.cpp`, titulo del Project
  Manager, `ProductName`/`FileDescription` en Windows, cabecera `--help`,
  comentarios de shaders convertidos). Ninguna logica compara `VERSION_NAME`.
- `module_config = ".lagoonic.1"`: es el sufijo ADITIVO oficial (igual que
  `.mono`, que llega por la misma via). Con el punto inicial obligatorio:
  `FULL_CONFIG = "4.7.2.stable.lagoonic.1"` y
  `--version` = `"4.7.2.stable.lagoonic.1.custom_build.<hash>"`.
- `website` SE MANTIENE (`godotengine.org`): solo se imprime en la cabecera,
  y conservarlo acredita a Godot segun exige `docs/FORK_POLICY.md` §7.

Efecto visible:

- `--version` pasa de `4.7.2.stable.custom_build.<hash>` a
  `4.7.2.stable.lagoonic.1.custom_build.<hash>`: sigue empezando por `4.`
  (deteccion de `Core/lagoonic-mcp/src/godot.ts` OK) y el `minor` extraido
  (`project.ts`) sigue siendo `4.7`. Las plantillas oficiales `4.7.2.stable`
  siguen valiendo (el check de `version.txt` solo exige >= 3 segmentos).
- Ventana del editor y Project Manager muestran "Lagoonic Engine".
- Detalle cosmetico conocido: el `versionName` Gradle de Android queda
  `4.7.2.stable..lagoonic.1` (doble punto por el `join(".")` de
  `config.gradle`); el `versionCode` no cambia (solo usa major/minor/patch/
  status). Igual que antes, el descargador de plantillas dentro del editor
  no encontrara mirrors para un build custom (ya pasaba con `custom_build`;
  las plantillas se instalan a mano segun `docs/ENGINE.md` §3.4).

## 2. `icon.svg` (NUEVO)

Marca propia 1024x1024 (hoja de laguna + arcos de agua) con la paleta medida
de `assets/branding/logo.png` (decodificado por histograma):
`#006430` (verde profundo, dominante), `#60a830` (hoja), `#a0d030` (ribete),
`#b0d860` (venas), `#085c30` (sombra), `#009ce0` (agua).
No sustituye a `misc/logo/*.svg` (iconos de tipo de nodo: prohibido tocarlos).

Efecto visible: ninguno por si solo (fuente para builds/herramientas; el
cableado —p. ej. copiarlo a rutas de build— es de los scripts, no de aqui).

## 3. `logo.svg` (NUEVO)

Logotipo horizontal: misma marca + "LAGOONIC" (claro `#eaf4ee`) + "ENGINE"
(agua `#009ce0`). Para cabeceras, acerca-de y web. Tampoco sustituye nada.

## 4. `splash.png` (NUEVO, 800x600 RGBA)

Lienzo TRANSPARENTE con solo el nenufar de `assets/branding/logo.png` a 340 px
de ancho, centrado. Sin fondo propio: asi el splash se funde con el color de
fondo de la ventana y no aparece un recuadro oscuro entre pantallas.

Generado solo con stdlib de Python (decodificacion, bilinear y composicion
a mano, sin dependencias): `assets/branding/logo.png` (1079x449) escalado a
560 px de ancho, centrado sobre fondo laguna oscuro `#10251b`.
Mismas dimensiones que `main/splash.png` para servir como FUENTE directa:
el script de build que lo active lo copiara a `main/splash.png` (splash de
boot de juegos) y/o `main/splash_editor.png` (splash del editor, hoy ausente:
`SConstruct` fuerza `no_editor_splash=True` sin ese fichero).

Efecto visible hoy: ninguno (no se sobrescribio `main/splash.png`;
hacerlo es decision del pipeline de build). Cuando se active: splash de
arranque Lagoonic en juegos y editor.

## 5. `app_icon.png` (NUEVO, 512x512 RGBA)

Mosaico macOS (squircle con margen y sombra) de
`Core/lagoonic-desktop/build-resources/app-tile.png` escalado a 512. No usar
un recorte a sangre del logo: el Dock no aplica mascara al icono que pone el
binario en tiempo de ejecucion y se ve como un cuadrado estirado.
(Activacion igual que el splash, via build.)

## 6. `defaults.cfg` (NUEVO — ajustes por defecto por CONFIGURACION)

Fija, sin tocar codigo del corazon:

- **Tema de color Lagoonic** (`[editor_settings]`):
  `interface/theme/color_preset="Custom"`,
  `base_color=Color(0.106, 0.169, 0.133, 1)` (`#1b2b22`, laguna),
  `accent_color=Color(0, 0.612, 0.878, 1)` (`#009ce0`, agua),
  `contrast=0.3` (igual que el default Godot, legibilidad intacta).
  `color_preset` DEBE ser `"Custom"`: con otro valor,
  `editor/themes/editor_theme_manager.cpp` sobrescribe base/accent con el
  preset e ignora estos colores. No se anade preset nuevo en C++ (corazon
  prohibido) ni se cambia ningun default Godot existente.
- **Puente activado** (`lagoonic/bridge/enabled=true`): clave NUEVA con
  prefijo `lagoonic/` (reservado, sin colision presente ni futura). La
  REGISTRA el modulo/addon Lagoonic (de otro agente); este fichero solo fija
  su valor por defecto. La seccion `[project]` documenta el override opt-in
  `[lagoonic]` para `project.godot` (Godot oficial lo conserva tal cual;
  proyectos sin Lagoonic quedan byte a byte iguales).

Quien lo aplica (cada plataforma con su mecanismo oficial, sin codigo del
motor): merge de `[editor_settings]` en `editor_settings-4.7.tres`
(`~/.config/godot/` en Linux,
`~/Library/Application Support/Godot/` en macOS) o
`GodotLib.setEditorSetting()` con las mismas claves en Android
(ver `Android/godot-lib/README.md`: ya existe ese camino para los colores).

## 7. Lo que NO se toco (prohibido)

Logica del motor (`core/`, `scene/`, `servers/`, `drivers/`, GDScript),
CLI/flags (`main/`), formatos (`scene/`, `project.godot`, `.godot/`),
import/export (`editor/export/`, `platform/*/export/`), `SConstruct`/
`methods.py`, `LICENSE.txt`/`COPYRIGHT.txt`/`AUTHORS.md`/`DONORS.md`,
iconos de tipo de nodo (`editor/icons/`, `misc/logo/`), `modules/lagoonic/`
ni `editor/lagoonic/` (de otros agentes). Sin commit.

## 8. Como verificar

```bash
cd Engine/godot
git status --short                # solo: M version.py, ?? lagoonic_branding/
git diff version.py               # 2 lineas: name y module_config
python3 -c "import methods; print(methods.get_version_info('', silent=True))"
file lagoonic_branding/*.png      # 800x600 y 128x128 RGBA
python3 -c "import xml.dom.minidom as m; [m.parse('lagoonic_branding/'+f) for f in ['icon.svg','logo.svg']]; print('SVG OK')"
```
