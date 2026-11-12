/**************************************************************************/
/*  lagoonic_pm_bridge.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            LAGOONIC ENGINE                             */
/**************************************************************************/

#include "lagoonic_pm_bridge.h"

#include "core/io/config_file.h"
#include "editor/asset_library/asset_library_editor_plugin.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_manager.h"
#include "editor/project_manager/quick_settings_dialog.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/texture_rect.h"

namespace {

// Acceso a miembros privados por instanciacion explicita de plantilla
// (permitido por el estandar C++; ver register_types.cpp).
template <typename Tag, typename Tag::type M>
struct LagoonicPMAccess {
	friend typename Tag::type lagoonic_pm_get(Tag) { return M; }
};

#define LAGOONIC_PM_MEMBER(m_tag, m_class, m_type, m_member)    \
	struct m_tag {                                              \
		typedef m_type m_class::*type;                          \
		friend type lagoonic_pm_get(m_tag);                     \
	};                                                          \
	template struct LagoonicPMAccess<m_tag, &m_class::m_member>;

#define LAGOONIC_PM_METHOD(m_tag, m_member)                     \
	struct m_tag {                                              \
		typedef void (ProjectManager::*type)();                 \
		friend type lagoonic_pm_get(m_tag);                     \
	};                                                          \
	template struct LagoonicPMAccess<m_tag, &ProjectManager::m_member>;

LAGOONIC_PM_MEMBER(PMList, ProjectManager, ProjectList *, project_list)
LAGOONIC_PM_MEMBER(PMAssetLib, ProjectManager, EditorAssetLibrary *, asset_library)
LAGOONIC_PM_MEMBER(PMProjectDialog, ProjectManager, ProjectDialog *, project_dialog)
LAGOONIC_PM_MEMBER(PMEraseAsk, ProjectManager, ConfirmationDialog *, erase_ask)
LAGOONIC_PM_MEMBER(PMEraseMissingAsk, ProjectManager, ConfirmationDialog *, erase_missing_ask)
LAGOONIC_PM_MEMBER(PMTagDialog, ProjectManager, ConfirmationDialog *, tag_manage_dialog)
LAGOONIC_PM_MEMBER(PMAskUpdate, ProjectManager, ConfirmationDialog *, ask_update_settings)
LAGOONIC_PM_MEMBER(ListProjects, ProjectList, Vector<ProjectList::Item>, _projects)
LAGOONIC_PM_MEMBER(ListConfig, ProjectList, ConfigFile, _config)
LAGOONIC_PM_MEMBER(ListSearch, ProjectList, String, _search_term)
LAGOONIC_PM_MEMBER(ListOrder, ProjectList, ProjectList::FilterOption, _order_option)
LAGOONIC_PM_MEMBER(PMQuickSettingsDialog, ProjectManager, QuickSettingsDialog *, quick_settings_dialog)
LAGOONIC_PM_MEMBER(ItemIcon, ProjectListItemControl, TextureRect *, project_icon)

LAGOONIC_PM_METHOD(PMNew, _new_project)
LAGOONIC_PM_METHOD(PMImport, _import_project)
LAGOONIC_PM_METHOD(PMScan, _scan_projects)
LAGOONIC_PM_METHOD(PMOpenCheck, _open_selected_projects_check_recovery_mode)
LAGOONIC_PM_METHOD(PMRun, _run_project)
LAGOONIC_PM_METHOD(PMRename, _rename_project)
LAGOONIC_PM_METHOD(PMDuplicate, _duplicate_project)
LAGOONIC_PM_METHOD(PMTags, _manage_project_tags)
LAGOONIC_PM_METHOD(PMErase, _erase_project)
LAGOONIC_PM_METHOD(PMEraseMissing, _erase_missing_projects)
LAGOONIC_PM_METHOD(PMShowInFM, _show_project_in_file_manager)

struct QSFullSettings {
	typedef void (QuickSettingsDialog::*type)();
	friend type lagoonic_pm_get(QSFullSettings);
};
template struct LagoonicPMAccess<QSFullSettings, &QuickSettingsDialog::_show_full_settings>;

struct PMOpenOptions {
	typedef void (ProjectManager::*type)(int);
	friend type lagoonic_pm_get(PMOpenOptions);
};
template struct LagoonicPMAccess<PMOpenOptions, &ProjectManager::_on_open_options_selected>;

#undef LAGOONIC_PM_MEMBER
#undef LAGOONIC_PM_METHOD

template <typename Tag>
void call_pm(ProjectManager *p_pm) {
	if (p_pm != nullptr) {
		(p_pm->*lagoonic_pm_get(Tag()))();
	}
}

const Vector<ProjectList::Item> empty_items;

} // namespace

namespace LagoonicPMBridge {

ProjectManager *as_project_manager(Control *p_control) {
	return Object::cast_to<ProjectManager>(p_control);
}

ProjectList *get_list(ProjectManager *p_pm) {
	return p_pm ? p_pm->*lagoonic_pm_get(PMList()) : nullptr;
}

const Vector<ProjectList::Item> &get_items(ProjectList *p_list) {
	return p_list ? p_list->*lagoonic_pm_get(ListProjects()) : empty_items;
}

String get_search_term(ProjectList *p_list) {
	return p_list ? p_list->*lagoonic_pm_get(ListSearch()) : String();
}

int get_order_option(ProjectList *p_list) {
	return p_list ? (int)(p_list->*lagoonic_pm_get(ListOrder())) : 0;
}

Ref<Texture2D> get_item_icon(const ProjectList::Item &p_item) {
	if (p_item.control == nullptr) {
		return Ref<Texture2D>();
	}
	TextureRect *icon = p_item.control->*lagoonic_pm_get(ItemIcon());
	return icon ? icon->get_texture() : Ref<Texture2D>();
}

bool select_only(ProjectList *p_list, const String &p_path) {
	if (p_list == nullptr) {
		return false;
	}
	const Vector<ProjectList::Item> &items = get_items(p_list);
	for (int i = 0; i < items.size(); i++) {
		if (items[i].path == p_path) {
			p_list->select_project(i, true);
			return true;
		}
	}
	return false;
}

void set_favorite(ProjectList *p_list, const String &p_path, bool p_favorite) {
	if (p_list == nullptr) {
		return;
	}
	Vector<ProjectList::Item> &items = p_list->*lagoonic_pm_get(ListProjects());
	for (int i = 0; i < items.size(); i++) {
		ProjectList::Item &item = items.write[i];
		if (item.path != p_path) {
			continue;
		}
		item.favorite = p_favorite;
		(p_list->*lagoonic_pm_get(ListConfig())).set_value(item.path, "favorite", p_favorite);
		p_list->save_config();
		if (item.control) {
			item.control->set_is_favorite(p_favorite);
		}
		p_list->sort_projects();
		p_list->update_dock_menu();
		return;
	}
}

void register_project(ProjectList *p_list, const String &p_path) {
	if (p_list == nullptr) {
		return;
	}
	for (const ProjectList::Item &item : get_items(p_list)) {
		if (item.path == p_path) {
			return;
		}
	}
	p_list->add_project(p_path, false);
	p_list->save_config();
	p_list->refresh_project(p_path);
	p_list->update_dock_menu();
}

Control *get_asset_library(ProjectManager *p_pm) {
	return p_pm ? p_pm->*lagoonic_pm_get(PMAssetLib()) : nullptr;
}

ProjectDialog *get_project_dialog(ProjectManager *p_pm) {
	return p_pm ? p_pm->*lagoonic_pm_get(PMProjectDialog()) : nullptr;
}

Vector<ConfirmationDialog *> get_list_dialogs(ProjectManager *p_pm) {
	Vector<ConfirmationDialog *> dialogs;
	if (p_pm != nullptr) {
		dialogs.push_back(p_pm->*lagoonic_pm_get(PMEraseAsk()));
		dialogs.push_back(p_pm->*lagoonic_pm_get(PMEraseMissingAsk()));
		dialogs.push_back(p_pm->*lagoonic_pm_get(PMTagDialog()));
		dialogs.push_back(p_pm->*lagoonic_pm_get(PMAskUpdate()));
	}
	return dialogs;
}

void new_project(ProjectManager *p_pm) { call_pm<PMNew>(p_pm); }
void import_project(ProjectManager *p_pm) { call_pm<PMImport>(p_pm); }
void scan_projects(ProjectManager *p_pm) { call_pm<PMScan>(p_pm); }
void run_selected(ProjectManager *p_pm) { call_pm<PMRun>(p_pm); }
void rename_selected(ProjectManager *p_pm) { call_pm<PMRename>(p_pm); }
void duplicate_selected(ProjectManager *p_pm) { call_pm<PMDuplicate>(p_pm); }
void manage_tags_selected(ProjectManager *p_pm) { call_pm<PMTags>(p_pm); }
void erase_selected(ProjectManager *p_pm) { call_pm<PMErase>(p_pm); }
void erase_missing(ProjectManager *p_pm) { call_pm<PMEraseMissing>(p_pm); }
void show_selected_in_file_manager(ProjectManager *p_pm) { call_pm<PMShowInFM>(p_pm); }
void show_all_editor_settings(ProjectManager *p_pm) {
	QuickSettingsDialog *dialog = p_pm ? p_pm->*lagoonic_pm_get(PMQuickSettingsDialog()) : nullptr;
	if (dialog != nullptr) {
		// Reinicios pedidos desde ahi siguen el flujo del gestor clasico.
		(dialog->*lagoonic_pm_get(QSFullSettings()))();
	}
}

void open_selected(ProjectManager *p_pm) {
	call_pm<PMOpenCheck>(p_pm);
}

void open_selected_verbose(ProjectManager *p_pm) {
	if (p_pm != nullptr) {
		(p_pm->*lagoonic_pm_get(PMOpenOptions()))(0);
	}
}

void open_selected_recovery(ProjectManager *p_pm) {
	if (p_pm != nullptr) {
		(p_pm->*lagoonic_pm_get(PMOpenOptions()))(1);
	}
}

} // namespace LagoonicPMBridge
