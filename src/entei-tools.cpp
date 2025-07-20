#include "entei-tools.h"
#include "entei-dialog.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "plugin-support.h"

static EnteiToolsDialog *dialog = nullptr;

static void entei_tools_menu_clicked(void *private_data)
{
	UNUSED_PARAMETER(private_data);

	// Check if dialog is null or has been destroyed
	if (!dialog) {
		dialog = new EnteiToolsDialog();
		// Clear static pointer when this dialog is destroyed
		QObject::connect(dialog, &QObject::destroyed, []() { dialog = nullptr; });
	}

	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void register_entei_tools_menu(void)
{
	obs_frontend_add_tools_menu_item("Entei Caption Provider", entei_tools_menu_clicked, nullptr);
	obs_log(LOG_INFO, "Entei Tools menu registered");
}

void unregister_entei_tools_menu(void)
{
	if (dialog) {
		dialog->close();
		delete dialog;
		dialog = nullptr;
	}

	obs_log(LOG_INFO, "Entei Tools menu unregistered");
}