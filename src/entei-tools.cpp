#include "entei-tools.h"
#include "websocket-client.h"
#include <obs-module.h>
#include "plugin-support.h"

#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QGroupBox>

class EnteiToolsDialog : public QDialog
{
	Q_OBJECT

public:
	EnteiToolsDialog(QWidget *parent = nullptr);
	~EnteiToolsDialog();

private slots:
	void onConnectClicked();
	void onDisconnectClicked();
	void onWebSocketUrlChanged();

private:
	void setupUI();
	void updateConnectionStatus(bool connected);
	void onWebSocketConnected(bool connected);
	void onWebSocketMessage(const char *message, size_t len);
	
	static void websocket_connect_callback(bool connected, void *user_data);
	static void websocket_message_callback(const char *message, size_t len, void *user_data);

	QLineEdit *websocketUrlEdit;
	QPushButton *connectButton;
	QPushButton *disconnectButton;
	QLabel *statusLabel;
	QTextEdit *logTextEdit;
	
	struct websocket_client *client;
	bool isConnected;
};

static EnteiToolsDialog *dialog = nullptr;

EnteiToolsDialog::EnteiToolsDialog(QWidget *parent)
	: QDialog(parent), client(nullptr), isConnected(false)
{
	setWindowTitle("Entei Caption Provider");
	setModal(false);
	resize(500, 400);
	
	setupUI();
}

EnteiToolsDialog::~EnteiToolsDialog()
{
	if (client) {
		websocket_client_disconnect(client);
		websocket_client_destroy(client);
		client = nullptr;
	}
}

void EnteiToolsDialog::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	
	// Connection group
	QGroupBox *connectionGroup = new QGroupBox("WebSocket Connection");
	QVBoxLayout *connectionLayout = new QVBoxLayout(connectionGroup);
	
	// URL input
	QHBoxLayout *urlLayout = new QHBoxLayout();
	urlLayout->addWidget(new QLabel("WebSocket URL:"));
	websocketUrlEdit = new QLineEdit("ws://saya:7175/socket/websocket");
	urlLayout->addWidget(websocketUrlEdit);
	connectionLayout->addLayout(urlLayout);
	
	// Connect/Disconnect buttons
	QHBoxLayout *buttonLayout = new QHBoxLayout();
	connectButton = new QPushButton("Connect");
	disconnectButton = new QPushButton("Disconnect");
	disconnectButton->setEnabled(false);
	
	buttonLayout->addWidget(connectButton);
	buttonLayout->addWidget(disconnectButton);
	buttonLayout->addStretch();
	connectionLayout->addLayout(buttonLayout);
	
	// Status
	QHBoxLayout *statusLayout = new QHBoxLayout();
	statusLayout->addWidget(new QLabel("Status:"));
	statusLabel = new QLabel("Disconnected");
	statusLayout->addWidget(statusLabel);
	statusLayout->addStretch();
	connectionLayout->addLayout(statusLayout);
	
	mainLayout->addWidget(connectionGroup);
	
	// Log group
	QGroupBox *logGroup = new QGroupBox("Message Log");
	QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
	
	logTextEdit = new QTextEdit();
	logTextEdit->setReadOnly(true);
	logTextEdit->setMaximumHeight(200);
	logLayout->addWidget(logTextEdit);
	
	mainLayout->addWidget(logGroup);
	
	// Connect signals
	connect(connectButton, &QPushButton::clicked, this, &EnteiToolsDialog::onConnectClicked);
	connect(disconnectButton, &QPushButton::clicked, this, &EnteiToolsDialog::onDisconnectClicked);
	connect(websocketUrlEdit, &QLineEdit::textChanged, this, &EnteiToolsDialog::onWebSocketUrlChanged);
}

void EnteiToolsDialog::onConnectClicked()
{
	QString url = websocketUrlEdit->text();
	if (url.isEmpty()) {
		logTextEdit->append("Error: WebSocket URL is empty");
		return;
	}
	
	if (client) {
		websocket_client_destroy(client);
		client = nullptr;
	}
	
	client = websocket_client_create(url.toUtf8().constData());
	if (!client) {
		logTextEdit->append("Error: Failed to create WebSocket client");
		return;
	}
	
	websocket_client_set_connect_callback(client, websocket_connect_callback, this);
	websocket_client_set_message_callback(client, websocket_message_callback, this);
	
	if (websocket_client_connect(client)) {
		logTextEdit->append(QString("Connecting to %1...").arg(url));
		connectButton->setEnabled(false);
	} else {
		logTextEdit->append("Error: Failed to initiate connection");
	}
}

void EnteiToolsDialog::onDisconnectClicked()
{
	if (client) {
		websocket_client_disconnect(client);
		logTextEdit->append("Disconnecting...");
	}
}

void EnteiToolsDialog::onWebSocketUrlChanged()
{
	// Enable connect button only if we have a URL and aren't connected
	connectButton->setEnabled(!websocketUrlEdit->text().isEmpty() && !isConnected);
}

void EnteiToolsDialog::updateConnectionStatus(bool connected)
{
	isConnected = connected;
	statusLabel->setText(connected ? "Connected" : "Disconnected");
	connectButton->setEnabled(!connected && !websocketUrlEdit->text().isEmpty());
	disconnectButton->setEnabled(connected);
}

void EnteiToolsDialog::onWebSocketConnected(bool connected)
{
	updateConnectionStatus(connected);
	
	if (connected) {
		logTextEdit->append("✓ Connected successfully");
	} else {
		logTextEdit->append("✗ Connection failed or disconnected");
	}
}

void EnteiToolsDialog::onWebSocketMessage(const char *message, size_t len)
{
	QString msg = QString::fromUtf8(message, len);
	logTextEdit->append(QString("← %1").arg(msg));
}

void EnteiToolsDialog::websocket_connect_callback(bool connected, void *user_data)
{
	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);
	QMetaObject::invokeMethod(dialog, [dialog, connected]() {
		dialog->onWebSocketConnected(connected);
	}, Qt::QueuedConnection);
}

void EnteiToolsDialog::websocket_message_callback(const char *message, size_t len, void *user_data)
{
	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);
	
	// Copy the message since it might not be valid after this function returns
	QString msg = QString::fromUtf8(message, len);
	
	QMetaObject::invokeMethod(dialog, [dialog, msg]() {
		dialog->onWebSocketMessage(msg.toUtf8().constData(), msg.length());
	}, Qt::QueuedConnection);
}

static void entei_tools_menu_clicked(void)
{
	if (!dialog) {
		dialog = new EnteiToolsDialog();
	}
	
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void register_entei_tools_menu(void)
{
	obs_frontend_add_tools_menu_item("Entei Caption Provider", entei_tools_menu_clicked);
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

#include "entei-tools.moc"