#include "entei-dialog.h"
#include "websocket-client.h"
#include "phoenix-protocol.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include "plugin-support.h"

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QCheckBox>
#include <QtCore/QDateTime>
#include <QtGui/QCloseEvent>
#include <chrono>
#include <functional>

EnteiToolsDialog::EnteiToolsDialog(QWidget *parent)
	: QDialog(parent),
	  client(nullptr),
	  isConnected(false),
	  message_ref_counter(0),
	  channel_joined(false),
	  heartbeatTimer(nullptr),
	  captionTimer(nullptr),
	  streamingActive(false)
{
	setWindowTitle("Entei Caption Provider");
	setModal(false);
	setMinimumSize(400, 350);
	// Don't set fixed size - will be restored from settings

	// Generate a unique join reference for this session
	join_ref = QString::number(QDateTime::currentMSecsSinceEpoch());

	// Setup heartbeat timer (Phoenix typically uses 30 second intervals)
	heartbeatTimer = new QTimer(this);
	heartbeatTimer->setInterval(30000); // 30 seconds
	connect(heartbeatTimer, &QTimer::timeout, this, &EnteiToolsDialog::sendHeartbeat);

	// Setup caption timer for continuous stream
	captionTimer = new QTimer(this);
	captionTimer->setInterval(1500); // 1.5 seconds
	connect(captionTimer, &QTimer::timeout, this, &EnteiToolsDialog::onCaptionTimer);

	// Register for OBS frontend events for auto-connect
	obs_frontend_add_event_callback(obs_frontend_event_callback, this);

	setupUI();
	loadSettings();
}

EnteiToolsDialog::~EnteiToolsDialog()
{
	// Stop timers first to prevent callbacks after destruction starts
	if (heartbeatTimer) {
		heartbeatTimer->stop();
	}
	if (captionTimer) {
		captionTimer->stop();
	}

	// Unregister from OBS frontend events
	obs_frontend_remove_event_callback(obs_frontend_event_callback, this);

	if (client) {
		websocket_client_disconnect(client);
		websocket_client_destroy(client);
		client = nullptr;
	}
}

void EnteiToolsDialog::closeEvent(QCloseEvent *event)
{
	// Disconnect first to avoid any signals during shutdown
	if (isConnected && client) {
		disconnect();
	}

	saveSettings();
	QDialog::closeEvent(event);
}

void EnteiToolsDialog::setupUI()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(10, 10, 10, 10);
	mainLayout->setSpacing(10);

	// Connection Settings Group
	QGroupBox *connectionGroup = new QGroupBox("WebSocket Connection", this);
	QGridLayout *connectionLayout = new QGridLayout(connectionGroup);
	connectionLayout->setColumnStretch(1, 1); // Make the input column stretch

	QLabel *urlLabel = new QLabel("URL:", this);
	urlLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	connectionLayout->addWidget(urlLabel, 0, 0);

	websocketUrlEdit = new QLineEdit(this);
	websocketUrlEdit->setPlaceholderText("ws://saya:7175/socket/websocket?vsn=2.0.0");
	connectionLayout->addWidget(websocketUrlEdit, 0, 1);

	QLabel *channelLabel = new QLabel("Channel:", this);
	channelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	connectionLayout->addWidget(channelLabel, 1, 0);

	channelEdit = new QLineEdit(this);
	channelEdit->setPlaceholderText("transcription:live");
	connectionLayout->addWidget(channelEdit, 1, 1);

	autoConnectCheckBox = new QCheckBox("Auto-start captions when streaming begins", this);
	connectionLayout->addWidget(autoConnectCheckBox, 2, 0, 1, 2);

	mainLayout->addWidget(connectionGroup);

	// Status Group
	QGroupBox *statusGroup = new QGroupBox("Status", this);
	QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);

	statusLabel = new QLabel("Not Connected", this);
	statusLabel->setStyleSheet("QLabel { font-weight: bold; }");
	statusLayout->addWidget(statusLabel);

	mainLayout->addWidget(statusGroup);

	// Control Buttons
	QHBoxLayout *buttonLayout = new QHBoxLayout();

	connectButton = new QPushButton("Start Captions", this);
	connectButton->setEnabled(false);
	buttonLayout->addWidget(connectButton);

	disconnectButton = new QPushButton("Stop Captions", this);
	disconnectButton->setEnabled(false);
	disconnectButton->setVisible(false); // Hide by default
	buttonLayout->addWidget(disconnectButton);

	QPushButton *closeButton = new QPushButton("Close", this);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
	buttonLayout->addWidget(closeButton);

	mainLayout->addLayout(buttonLayout);

	// Message Log group
	QGroupBox *logGroup = new QGroupBox("Message Log", this);
	QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

	logTextEdit = new QTextEdit(this);
	logTextEdit->setReadOnly(true);
	logTextEdit->setMinimumHeight(100);
	logTextEdit->setMaximumHeight(150);
	logLayout->addWidget(logTextEdit);

	mainLayout->addWidget(logGroup);

	// Connect signals
	connect(connectButton, &QPushButton::clicked, this, &EnteiToolsDialog::onConnectClicked);
	connect(disconnectButton, &QPushButton::clicked, this, &EnteiToolsDialog::onDisconnectClicked);
	connect(websocketUrlEdit, &QLineEdit::textChanged, this, &EnteiToolsDialog::onWebSocketUrlChanged);
	// Channel is saved when dialog closes, no need for auto-save on each keystroke
	connect(autoConnectCheckBox, &QCheckBox::toggled, this, &EnteiToolsDialog::onAutoConnectToggled);

	// Initial state
	updateConnectionStatus(false);
}

void EnteiToolsDialog::loadSettings()
{
	// Load from OBS user config
#if LIBOBS_API_MAJOR_VER >= 31
	config_t *config = obs_frontend_get_user_config();
#else
	config_t *config = obs_frontend_get_profile_config();
#endif

	if (!config) {
		obs_log(LOG_WARNING, "Failed to get OBS config for loading settings");
		return;
	}

	const char *url = config_get_string(config, "EnteiCaptionProvider", "WebSocketUrl");
	if (url && strlen(url) > 0) {
		websocketUrlEdit->setText(url);
	}

	const char *channel = config_get_string(config, "EnteiCaptionProvider", "Channel");
	if (channel && strlen(channel) > 0) {
		channelEdit->setText(channel);
	}

	bool autoConnect = config_get_bool(config, "EnteiCaptionProvider", "AutoConnect");
	autoConnectCheckBox->setChecked(autoConnect);

	// Restore window geometry
	const char *geometryStr = config_get_string(config, "EnteiCaptionProvider", "DialogGeometry");
	if (geometryStr && strlen(geometryStr) > 0) {
		QByteArray geometry = QByteArray::fromBase64(geometryStr);
		restoreGeometry(geometry);
	} else {
		// Default size on first launch
		resize(450, 400);
	}
}

void EnteiToolsDialog::saveSettings()
{
	// Save to OBS user config
#if LIBOBS_API_MAJOR_VER >= 31
	config_t *config = obs_frontend_get_user_config();
#else
	config_t *config = obs_frontend_get_profile_config();
#endif

	if (!config) {
		obs_log(LOG_WARNING, "Failed to get OBS config for saving settings");
		return;
	}

	// Ensure UI elements exist before accessing them
	if (!websocketUrlEdit || !channelEdit || !autoConnectCheckBox) {
		obs_log(LOG_WARNING, "UI elements not available for saving settings");
		return;
	}

	std::string urlStdString = websocketUrlEdit->text().toStdString();
	std::string channelStdString = channelEdit->text().toStdString();
	config_set_string(config, "EnteiCaptionProvider", "WebSocketUrl", urlStdString.c_str());
	config_set_string(config, "EnteiCaptionProvider", "Channel", channelStdString.c_str());
	config_set_bool(config, "EnteiCaptionProvider", "AutoConnect", autoConnectCheckBox->isChecked());

	// Save window geometry
	QByteArray geometry = saveGeometry();
	std::string geometryStr = geometry.toBase64().constData();
	config_set_string(config, "EnteiCaptionProvider", "DialogGeometry", geometryStr.c_str());

	config_save(config);
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

	// Stop timers
	if (heartbeatTimer) {
		heartbeatTimer->stop();
	}
	if (captionTimer) {
		captionTimer->stop();
	}
}

void EnteiToolsDialog::onWebSocketUrlChanged()
{
	// Enable connect button only if we have a URL and aren't connected
	connectButton->setEnabled(!websocketUrlEdit->text().isEmpty() && !isConnected);
}

void EnteiToolsDialog::onAutoConnectToggled(bool enabled)
{
	if (enabled) {
		logTextEdit->append("Auto-captions enabled - will start when streaming begins");

		// If already streaming, connect immediately
		if (obs_frontend_streaming_active()) {
			if (!isConnected) {
				onConnectClicked();
			}
		}
	} else {
		logTextEdit->append("Auto-captions disabled");
	}

	// Update status to reflect auto-connect state
	updateConnectionStatus(isConnected);
}

void EnteiToolsDialog::updateConnectionStatus(bool connected)
{
	isConnected = connected;

	if (connected) {
		statusLabel->setText("Connected - Captions Active");
		statusLabel->setStyleSheet("QLabel { font-weight: bold; color: green; }");
		connectButton->setVisible(false);
		disconnectButton->setVisible(true);
		disconnectButton->setEnabled(true);
	} else {
		if (autoConnectCheckBox->isChecked()) {
			statusLabel->setText("Auto-Connect: Waiting for stream");
			statusLabel->setStyleSheet("QLabel { font-weight: bold; color: blue; }");
		} else {
			statusLabel->setText("Not Connected");
			statusLabel->setStyleSheet("QLabel { font-weight: bold; }");
		}
		connectButton->setVisible(true);
		connectButton->setEnabled(!websocketUrlEdit->text().isEmpty());
		disconnectButton->setVisible(false);
	}
}

void EnteiToolsDialog::onWebSocketConnected(bool connected)
{
	updateConnectionStatus(connected);

	if (connected) {
		logTextEdit->append("✓ Connected successfully");

		// Send initial heartbeat to establish Phoenix connection
		sendHeartbeat();

		// Start periodic heartbeat timer
		heartbeatTimer->start();

		// Start caption timer if we're already streaming
		if (obs_frontend_streaming_active() && captionTimer) {
			streamingActive = true;
			captionTimer->start();
		}

		// Auto-join the specified channel
		QString channel = channelEdit->text().trimmed();
		if (!channel.isEmpty()) {
			joinChannel(channel);
		}
	} else {
		logTextEdit->append("✗ Connection failed or disconnected");
		current_channel.clear();
		channel_joined = false;

		// Stop timers
		heartbeatTimer->stop();
		if (captionTimer) {
			captionTimer->stop();
		}
		pendingCaptionText.clear();
	}
}

void EnteiToolsDialog::onWebSocketMessage(const QString &message)
{
	logTextEdit->append(QString("← %1").arg(message));

	// Process the message using Phoenix protocol
	processPhoenixMessage(message.toUtf8().constData());
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
	// Add defensive check in case timer fires after disconnect
	if (!isConnected || !client) {
		return;
	}

	QString msg_ref = getNextMessageRef();
	char *heartbeat_json = phoenix_create_heartbeat_json(msg_ref.toUtf8().constData());

	if (heartbeat_json) {
		sendPhoenixMessage(heartbeat_json);
		bfree(heartbeat_json);
	}
}

void EnteiToolsDialog::onCaptionTimer()
{
	// Only send captions if we're streaming
	obs_output_t *streaming_output = obs_frontend_get_streaming_output();
	if (!streaming_output) {
		return;
	}

	// Check if output is active
	if (!obs_output_active(streaming_output)) {
		obs_output_release(streaming_output);
		return;
	}

	// Send caption with current text
	// Duration is 2.0 seconds to overlap with next timer interval (1.5s)
	// Don't clear - keep sending same text until new caption arrives
	if (!pendingCaptionText.isEmpty()) {
		// CEA-708 Caption Length Validation
		// Limit to 96 characters total (approximately 32 chars per line for 3 lines)
		const int MAX_CAPTION_LENGTH = 96;
		QByteArray captionBytes = pendingCaptionText.toUtf8();
		if (captionBytes.size() > MAX_CAPTION_LENGTH) {
			captionBytes.truncate(MAX_CAPTION_LENGTH);
			// Log truncation for debugging
			logTextEdit->append(QString("⚠ Caption truncated to %1 chars").arg(MAX_CAPTION_LENGTH));
		}
		
		obs_output_output_caption_text2(streaming_output, captionBytes.constData(), 2.0);
		// Debug log - remove after testing
		static QString lastLogged;
		if (pendingCaptionText != lastLogged) {
			logTextEdit->append(QString("→ Sending caption: %1").arg(pendingCaptionText));
			lastLogged = pendingCaptionText;
		}
	}

	obs_output_release(streaming_output);
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
				channel_joined = true;
				logTextEdit->append(
					QString("✓ Joined channel: %1").arg(message.topic ? message.topic : "unknown"));
			} else {
				channel_joined = false;
				logTextEdit->append(
					QString("✗ Failed to join channel: %1").arg(status ? status : "unknown error"));
			}
		} else if (message.event && strcmp(message.event, "new_transcription") == 0) {
			// Handle transcription messages
			if (message.payload) {
				cJSON *text_item = cJSON_GetObjectItem(message.payload, "text");

				if (cJSON_IsString(text_item)) {
					const char *caption_text = cJSON_GetStringValue(text_item);
					if (caption_text) {
						logTextEdit->append(QString("Caption: %1").arg(caption_text));

						// Update pending caption text instead of sending immediately
						pendingCaptionText = QString::fromUtf8(caption_text);
					}
				}
			}
		}

		phoenix_message_free(&message);
	}
}

void EnteiToolsDialog::websocket_connect_callback(bool connected, void *user_data)
{
	if (!user_data) {
		obs_log(LOG_WARNING, "WebSocket connect callback received null user_data");
		return;
	}

	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);
	QMetaObject::invokeMethod(
		dialog, [dialog, connected]() { dialog->onWebSocketConnected(connected); }, Qt::QueuedConnection);
}

void EnteiToolsDialog::websocket_message_callback(const char *message, size_t len, void *user_data)
{
	if (!user_data || !message) {
		obs_log(LOG_WARNING, "WebSocket message callback received null parameters");
		return;
	}

	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(user_data);

	// Copy the message since it might not be valid after this function returns
	QString msg = QString::fromUtf8(message, len);

	QMetaObject::invokeMethod(dialog, [dialog, msg]() { dialog->onWebSocketMessage(msg); }, Qt::QueuedConnection);
}

void EnteiToolsDialog::obs_frontend_event_callback(enum obs_frontend_event event, void *private_data)
{
	EnteiToolsDialog *dialog = static_cast<EnteiToolsDialog *>(private_data);
	if (!dialog || !dialog->autoConnectCheckBox) {
		return;
	}

	// Only handle events if auto-connect is enabled
	if (!dialog->autoConnectCheckBox->isChecked()) {
		return;
	}

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		dialog->streamingActive = true;
		// Start caption timer when streaming starts
		if (dialog->captionTimer && dialog->isConnected) {
			dialog->captionTimer->start();
		}
		if (!dialog->isConnected) {
			QMetaObject::invokeMethod(
				dialog,
				[dialog]() {
					dialog->logTextEdit->append("Stream started - starting captions...");
					dialog->onConnectClicked();
				},
				Qt::QueuedConnection);
		}
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		dialog->streamingActive = false;
		// Stop caption timer when streaming stops
		if (dialog->captionTimer) {
			dialog->captionTimer->stop();
		}
		if (dialog->isConnected) {
			QMetaObject::invokeMethod(
				dialog,
				[dialog]() {
					dialog->logTextEdit->append("Stream stopped - stopping captions...");
					dialog->onDisconnectClicked();
				},
				Qt::QueuedConnection);
		}
		break;
	default:
		break;
	}
}
