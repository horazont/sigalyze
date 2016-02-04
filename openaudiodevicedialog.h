#ifndef OPENAUDIODEVICEDIALOG_H
#define OPENAUDIODEVICEDIALOG_H

#include "ui_openaudiodevicedialog.h"

#include <QAudioDeviceInfo>
#include <QStandardItemModel>

class OpenAudioDeviceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenAudioDeviceDialog(QWidget *parent = 0);

private:
    Ui::OpenAudioDeviceDialog ui;
    QList<QAudioDeviceInfo> m_devices;
    QStandardItemModel m_model;
    QAudioDeviceInfo m_selected_device;
    QAudioFormat m_selected_format;

private:
    void update_buttons();

public:
    void refresh();
    QAudioDeviceInfo device() const;
    QAudioFormat format() const;

public slots:
    void on_device_list_selection_model_current_changed(QModelIndex current,
                                                        QModelIndex previous);
private slots:
    void on_sample_rate_currentIndexChanged(int index);
    void on_sample_size_currentIndexChanged(int index);
    void on_sample_type_currentIndexChanged(int index);

    // QDialog interface
public slots:
    void accept();
};

#endif // OPENAUDIODEVICEDIALOG_H
