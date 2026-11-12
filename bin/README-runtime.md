# Engine/godot/bin — binarios del fork y runtime PC esperado

Esta carpeta es **salida de compilación** (ignorada por git, ver
`Engine/godot/.gitignore` `[Bb]in/`; solo este README está versionado).
`scripts/build-godot-mac.sh` deja aquí el editor, p. ej.
`godot.macos.editor.arm64` (~140 MB). Nombres por plataforma
(ver `Core/lagoonic-desktop/src/main/godot-bin.ts:engineGodotNames`):

- macOS: `godot.macos.editor.arm64` / `godot.macos.editor.x86_64`
- Windows: `godot.windows.editor.x86_64.exe` / `godot.windows.editor.arm64.exe`
- Linux: `godot.linuxbsd.editor.x86_64` / `godot.linuxbsd.editor.arm64`

## Layout que el bundle PC espera junto al binario

`scripts/build-lagoonic-pc.sh` copia el binario de aquí y arma, **fuera** de
esta carpeta (`release/pc/<platform>-<arch>/`), una de estas estructuras.
El fork en ejecución la localiza desde `OS::get_executable_path()`:

macOS (`Lagoonic.app/Contents/MacOS/Lagoonic`):

```text
Contents/Resources/runtime/
  opencode/opencode
  lagoonic-mcp/lagoonic-mcp-darwin-<arch> (+ gdscript/, addon/)
  export-templates/   (4.7.2, SHA-512 verificado)
  manifest.json
```

Windows/Linux (`bin/lagoonic[.exe]`):

```text
runtime/
  opencode/opencode[.exe]
  lagoonic-mcp/lagoonic-mcp-<platform>-<arch>[.exe] (+ gdscript/, addon/)
  export-templates/   (4.7.2, SHA-512 verificado)
  manifest.json
```

Regla: runtime = `executable_dir/../Resources/runtime` en macOS,
`executable_dir/../runtime` en Windows/Linux.
Detalle completo en `docs/PC_PACKAGING.md`.
