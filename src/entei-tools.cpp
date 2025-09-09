#include "entei-tools.h"
#include "entei-dialog.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "plugin-support.h"
#include <QtWidgets/QWidget>

static EnteiToolsDialog *dialog = nullptr;

static void entei_tools_menu_clicked(void *private_data)
{
	UNUSED_PARAMETER(private_data);

	// Toggle visibility instead of creating/destroying
	if (dialog) {
		dialog->setVisible(!dialog->isVisible());
		if (dialog->isVisible()) {
			dialog->raise();
			dialog->activateWindow();
		}
	}
}

void register_entei_tools_menu(void)
{
	// Create dialog once at registration
	QWidget *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	dialog = new EnteiToolsDialog(main_window);

	obs_frontend_add_tools_menu_item("Entei Caption Provider", entei_tools_menu_clicked, nullptr);
	obs_log(LOG_INFO, "Entei Tools menu registered");
}

void unregister_entei_tools_menu(void)
{
	// Let Qt parent ownership handle deletion
	// Dialog will be automatically deleted when parent (main_window) is destroyed
	if (dialog) {
		dialog->close();
		// Don't delete - parent will handle it
		dialog = nullptr;
	}

	obs_log(LOG_INFO, "Entei Tools menu unregistered");
}
