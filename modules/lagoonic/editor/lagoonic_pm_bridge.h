/**************************************************************************/
/*  lagoonic_pm_bridge.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#pragma once

// Puente al Project Manager clasico de Godot.
//
// El inicio de Lagoonic (LagoonicVibeHome) se monta encima del ProjectManager
// y reutiliza su lista de proyectos, sus dialogos (crear, importar, renombrar,
// duplicar, etiquetas, quitar, migracion, modo recuperacion), la biblioteca de
// plantillas y los ajustes rapidos. Todo eso es privado en upstream y la
// politica del fork prohibe tocar editor/, asi que se accede con el mismo
// truco de instanciacion explicita de plantillas que usa register_types.cpp.

#include "editor/project_manager/project_list.h"

class Control;
class ConfirmationDialog;
class ProjectDialog;
class ProjectManager;
class Texture2D;

namespace LagoonicPMBridge {

ProjectManager *as_project_manager(Control *p_control);

ProjectList *get_list(ProjectManager *p_pm);
const Vector<ProjectList::Item> &get_items(ProjectList *p_list);
String get_search_term(ProjectList *p_list);
int get_order_option(ProjectList *p_list);
Ref<Texture2D> get_item_icon(const ProjectList::Item &p_item);

// Selecciona solo ese proyecto en la lista clasica; false si no esta.
bool select_only(ProjectList *p_list, const String &p_path);
void set_favorite(ProjectList *p_list, const String &p_path, bool p_favorite);
// Registra la carpeta en projects.cfg a traves de la lista (sin pisar su copia en memoria).
void register_project(ProjectList *p_list, const String &p_path);

Control *get_asset_library(ProjectManager *p_pm);
ProjectDialog *get_project_dialog(ProjectManager *p_pm);
Vector<ConfirmationDialog *> get_list_dialogs(ProjectManager *p_pm);

// Acciones del gestor clasico (las que dependen de la seleccion actual).
void new_project(ProjectManager *p_pm);
void import_project(ProjectManager *p_pm);
void scan_projects(ProjectManager *p_pm);
void open_selected(ProjectManager *p_pm); // Con avisos de version, migracion y modo recuperacion.
void open_selected_verbose(ProjectManager *p_pm);
void open_selected_recovery(ProjectManager *p_pm);
void run_selected(ProjectManager *p_pm);
void rename_selected(ProjectManager *p_pm);
void duplicate_selected(ProjectManager *p_pm);
void manage_tags_selected(ProjectManager *p_pm);
void erase_selected(ProjectManager *p_pm);
void erase_missing(ProjectManager *p_pm);
void show_selected_in_file_manager(ProjectManager *p_pm);
// Dialogo completo de ajustes del editor (el "Edit All Settings" de Godot).
void show_all_editor_settings(ProjectManager *p_pm);

} // namespace LagoonicPMBridge
