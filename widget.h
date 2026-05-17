#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QMqttClient>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Play;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:

    void onMqttConnected();

    void on_connectBtn_clicked();

private:
    Ui::Widget *ui;

    QMqttClient *mqttClient;
    Play *playWindow;
};

#endif
