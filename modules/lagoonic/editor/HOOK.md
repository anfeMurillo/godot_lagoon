# HOOK — Registro del panel "Lagoonic" (resuelto dentro del módulo)

El hook manual de 4 lineas (include + `memnew` en `editor/editor_node.cpp`
+ `SConscript` en `editor/SCsub`) ESTA PROHIBIDO: violaria FORK_POLICY
(no tocar `editor/` upstream). No aplicarlo.

Estado (cierre): implementado en `modules/lagoonic/` — `LagoonicBridgePlugin`
crea el dock en `NOTIFICATION_ENTER_TREE` (`_ensure_dock`: `memnew` +
`add_dock`) y lo libera en `NOTIFICATION_EXIT_TREE` (`_free_dock`:
`remove_dock` + `memdelete`). `register_types.cpp` hace `GDREGISTER_CLASS`
del dock y `add_by_type<LagoonicBridgePlugin>()`. El `LagoonicAgentDock`
(Chat/Crew, CLIs, Proveedores) vive embebido como hijo del `LagoonicDock`.
Nada fuera de `modules/lagoonic/`.
