#pragma once

#include <QtWidgets/QDialog>
#include <QtCore/QTimer>
#include <QtCore/QString>
#include <QtCore/QMap>
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
	void onAutoConnectToggled(bool enabled);
	void onCaptionTimer();

private:
	void setupUI();
	void loadSettings();
	void saveSettings();
	void updateConnectionStatus(bool connected);
	void onWebSocketConnected(bool connected);
	void onWebSocketMessage(const QString &message);

	// WebSocket protocol helpers
	void sendPing();
	void processWebSocketMessage(const char *json);

	static void websocket_connect_callback(bool connected, void *user_data);
	static void websocket_message_callback(const char *message, size_t len, void *user_data);
	static void obs_frontend_event_callback(enum obs_frontend_event event, void *private_data);

	QLineEdit *websocketUrlEdit;
	QPushButton *connectButton;
	QPushButton *disconnectButton;
	QLabel *statusLabel;
	QTextEdit *logTextEdit;
	QCheckBox *autoConnectCheckBox;

	struct websocket_client *client;
	bool isConnected;

	// WebSocket state
	bool channel_joined;

	// Ping timer for WebSocket connection
	QTimer *heartbeatTimer;

	// Caption stream management
	QTimer *captionTimer;
	QString pendingCaptionText;
	bool streamingActive;

	// WhisperLive segment tracking
	struct CaptionSegment {
		QString text;
		double segment_id;
		bool is_final;
		bool is_revision;
		qint64 timestamp;
	};
	QMap<double, CaptionSegment> segments;
	QString lastComposedCaption;
	QString buildCaptionFromSegments();
};
