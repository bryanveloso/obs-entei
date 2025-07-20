#pragma once

#include <QtWidgets/QDialog>
#include <QtCore/QTimer>
#include <QtCore/QString>
#include <obs-frontend-api.h>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QLabel;
class QTextEdit;
class QCheckBox;
class QCloseEvent;
QT_END_NAMESPACE

struct websocket_client;

class EnteiToolsDialog : public QDialog {
	Q_OBJECT

public:
	EnteiToolsDialog(QWidget *parent = nullptr);
	~EnteiToolsDialog();

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void onConnectClicked();
	void onDisconnectClicked();
	void onWebSocketUrlChanged();
	void onChannelChanged();
	void onAutoConnectToggled(bool enabled);

private:
	void setupUI();
	void loadSettings();
	void saveSettings();
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
	static void obs_frontend_event_callback(enum obs_frontend_event event, void *private_data);

	QLineEdit *websocketUrlEdit;
	QLineEdit *channelEdit;
	QPushButton *connectButton;
	QPushButton *disconnectButton;
	QLabel *statusLabel;
	QTextEdit *logTextEdit;
	QCheckBox *autoConnectCheckBox;

	struct websocket_client *client;
	bool isConnected;

	// Phoenix protocol state
	int message_ref_counter;
	QString join_ref;
	QString current_channel;
	bool channel_joined;

	// Heartbeat timer for Phoenix connection
	QTimer *heartbeatTimer;
};

