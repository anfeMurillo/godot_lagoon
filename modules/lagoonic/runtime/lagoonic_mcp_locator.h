/**************************************************************************/
/*  lagoonic_mcp_locator.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Finds lagoonic-mcp (the server that gives coding agents the Godot tools)
// and the editor plugin shipped with it.
//
// Order:
//   1. LAGOONIC_MCP: path to the server binary or to its build/index.js.
//   2. The runtime packaged with the editor: <exe>/../Resources/runtime on
//      macOS, <exe>/../runtime elsewhere, in lagoonic-mcp/lagoonic-mcp-*.
//   3. A source checkout: Core/lagoonic-mcp/build/index.js above the
//      executable (Engine/godot/bin), run with the host's Node.js.
//   4. `lagoonic-mcp` installed on the host (PATH and the usual folders).
class LagoonicMcpLocator {
public:
	// Program and leading arguments that start the server; the caller adds
	// `--project <dir>`. Empty when the server is not on this machine.
	static Vector<String> command();

	// Folder of the editor plugin (holds plugin.cfg), or empty.
	static String addon_dir();

	// Copies the plugin to <project>/addons/lagoonic, writing only the
	// files that differ. True when the project has it afterwards.
	static bool install_addon(const String &p_project_dir);
};
