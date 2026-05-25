#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMqttClient>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Play;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

    // 设置断连延迟时间（毫秒）
    void setDisconnectDelay(int msec);

private slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onDisconnectDelayTimeout();
    void on_connectBtn_clicked();

private:
    Ui::Widget *ui;
    QMqttClient *mqttClient;
    Play *playWindow;

    // 断连延迟相关
    QTimer *disconnectDelayTimer;
    int disconnectDelayMs;               // 用户可配置的延迟时间（毫秒）

    // 保存上一次成功连接的参数，用于自动重连
    QString lastHost;
    quint16 lastPort;
    QString lastClientId;
    QString lastUsername;
    QString lastPassword;
};

#endif // WIDGET_H
