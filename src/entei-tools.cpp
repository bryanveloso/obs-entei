#include "entei-tools.h"
#include "websocket-client.h"
#include "phoenix-protocol.h"
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
#include <QtCore/QDateTime>

class EnteiToolsDialog : public QDialog {
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

	// Phoenix protocol helpers
	QString getNextMessageRef();
	void sendPhoenixMessage(const char *json_message);
	void sendHeartbeat();
	void joinChannel(const QString &channel);
	void processPhoenixMessage(const char *json);

	static void websocket_connect_callback(bool connected, void *user_data);
	static void websocket_message_callback(const char *message, size_t len, void *user_data);

	QLineEdit *websocketUrlEdit;
	QPushButton *connectButton;
	QPushButton *disconnectButton;
	QLabel *statusLabel;
	QTextEdit *logTextEdit;

	struct websocket_client *client;
	bool isConnected;

	// Phoenix protocol state
	int message_ref_counter;
	QString join_ref;
	QString current_channel;
};

static EnteiToolsDialog *dialog = nullptr;

EnteiToolsDialog::EnteiToolsDialog(QWidget *parent)
	: QDialog(parent),
	  client(nullptr),
	  isConnected(false),
	  message_ref_counter(0)
{
	setWindowTitle("Entei Caption Provider");
	setModal(false);
	resize(500, 400);

	// Generate a unique join reference for this session
	join_ref = QString::number(QDateTime::currentMSecsSinceEpoch());

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
	websocketUrlEdit = new QLineEdit("ws://saya:7175/socket/websocket?vsn=2.0.0");
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

		// Send initial heartbeat to establish Phoenix connection
		sendHeartbeat();

		// Auto-join a test channel (you can modify this)
		joinChannel("captions:lobby");
	} else {
		logTextEdit->append("✗ Connection failed or disconnected");
		current_channel.clear();
	}
}

void EnteiToolsDialog::onWebSocketMessage(const char *message, size_t len)
{
	QString msg = QString::fromUtf8(message, len);
	logTextEdit->append(QString("← %1").arg(msg));

	// Process the message using Phoenix protocol
	processPhoenixMessage(message);
}

QString EnteiToolsDialog::getNextMessageRef()
{
	return QString::number(++message_ref_counter);
}

void EnteiToolsDialog::sendPhoenixMessage(const char *json_message)
{
	if (!client || !isConnected || !json_message) {
		return;
	}

	logTextEdit->append(QString("→ %1").arg(json_message));
	websocket_client_send(client, json_message);
}

void EnteiToolsDialog::sendHeartbeat()
{
	QString msg_ref = getNextMessageRef();
	char *heartbeat_json = phoenix_create_heartbeat_json(msg_ref.toUtf8().constData());

	if (heartbeat_json) {
		sendPhoenixMessage(heartbeat_json);
		bfree(heartbeat_json);
	}
}

void EnteiToolsDialog::joinChannel(const QString &channel)
{
	if (channel.isEmpty()) {
		return;
	}

	QString msg_ref = getNextMessageRef();
	cJSON *payload = cJSON_CreateObject();

	char *join_json = phoenix_create_join_json(join_ref.toUtf8().constData(), msg_ref.toUtf8().constData(),
						   channel.toUtf8().constData(), payload);

	if (join_json) {
		current_channel = channel;
		sendPhoenixMessage(join_json);
		bfree(join_json);
	}

	cJSON_Delete(payload);
}

void EnteiToolsDialog::processPhoenixMessage(const char *json)
{
	phoenix_message_t message;

	if (phoenix_parse_message(json, &message)) {
		if (phoenix_is_heartbeat_reply(&message)) {
			logTextEdit->append("✓ Heartbeat acknowledged");
		} else if (phoenix_is_join_reply(&message)) {
			const char *status = phoenix_get_reply_status(&message);
			if (status && strcmp(status, "ok") == 0) {
				logTextEdit->append(
					QString("✓ Joined channel: %1").arg(message.topic ? message.topic : "unknown"));
			} else {
				logTextEdit->append(
					QString("✗ Failed to join channel: %1").arg(status ? status : "unknown error"));
			}
		} else {
			// Handle other message types (captions, etc.)
			if (message.event && message.payload) {
				const char *event = message.event;
				cJSON *text_item = cJSON_GetObjectItem(message.payload, "text");

				if (cJSON_IsString(text_item)) {
					const char *caption_text = cJSON_GetStringValue(text_item);
					if (caption_text) {
						logTextEdit->append(QString("Caption: %1").arg(caption_text));
						// TODO: Send to OBS caption system
					}
				}
			}
		}

		phoenix_message_free(&message);
	}
}

void EnteiToolsDialog::websocket_connect_callback(bool connected, void *user_data)
{
	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);
	QMetaObject::invokeMethod(
		dialog, [dialog, connected]() { dialog->onWebSocketConnected(connected); }, Qt::QueuedConnection);
}

void EnteiToolsDialog::websocket_message_callback(const char *message, size_t len, void *user_data)
{
	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);

	// Copy the message since it might not be valid after this function returns
	QString msg = QString::fromUtf8(message, len);

	QMetaObject::invokeMethod(
		dialog, [dialog, msg]() { dialog->onWebSocketMessage(msg.toUtf8().constData(), msg.length()); },
		Qt::QueuedConnection);
}

static void entei_tools_menu_clicked(void *private_data)
{
	UNUSED_PARAMETER(private_data);

	if (!dialog) {
		dialog = new EnteiToolsDialog();
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

#include "entei-tools.moc"
