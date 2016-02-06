#include "openaudiodevicedialog.h"

#include <iostream>
#include <string>

#include <QAudioDeviceInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItemModel>


OpenAudioDeviceDialog::OpenAudioDeviceDialog(QWidget *parent) :
    QDialog(parent)
{
    ui.setupUi(this);
    ui.device_list->setModel(&m_model);
    connect(ui.device_list->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &OpenAudioDeviceDialog::device_list_selection_model_current_changed);
}

void OpenAudioDeviceDialog::update_buttons()
{
    ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                ui.device_list->selectionModel()->currentIndex().isValid() and
                ui.sample_rate->currentIndex() >= 0 and
                ui.sample_size->currentIndex() >= 0 and
                ui.sample_type->currentIndex() >= 0);
}

void OpenAudioDeviceDialog::refresh()
{
    m_model.clear();
    m_devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    m_model.insertRows(0, m_devices.size());
    m_model.setColumnCount(3);
    QMap<int, QVariant> item_data;
    for (int i = 0; i < m_devices.size(); ++i) {
        item_data.clear();
        const QAudioDeviceInfo &device_info = m_devices[i];
        item_data[Qt::DisplayRole] = device_info.deviceName();
        m_model.setItemData(m_model.index(i, 0), item_data);

        QAudioFormat preferred = device_info.preferredFormat();

        item_data.clear();
        item_data[Qt::DisplayRole] = QString::number(preferred.sampleRate());
        m_model.setItemData(m_model.index(i, 1), item_data);

        item_data.clear();
        item_data[Qt::DisplayRole] = QString::number(preferred.sampleSize());
        m_model.setItemData(m_model.index(i, 2), item_data);
    }

    m_model.setHeaderData(0, Qt::Horizontal, "Device name");
    m_model.setHeaderData(1, Qt::Horizontal, "Preferred sample rate");
    m_model.setHeaderData(2, Qt::Horizontal, "Preferred sample bits");

    ui.device_list->resizeColumnsToContents();
}

QAudioDeviceInfo OpenAudioDeviceDialog::device() const
{
    return m_selected_device;
}

QAudioFormat OpenAudioDeviceDialog::format() const
{
    return m_selected_format;
}

void OpenAudioDeviceDialog::device_list_selection_model_current_changed(
        QModelIndex current,
        QModelIndex)
{
    if (!current.isValid()) {
        ui.sample_size->setEnabled(false);
        ui.sample_rate->setEnabled(false);
        ui.sample_type->setEnabled(false);
        return;
    }

    ui.sample_rate->clear();
    ui.sample_size->clear();
    ui.sample_type->clear();


    const QAudioDeviceInfo &device = m_devices[current.row()];
    QAudioFormat preferred = device.preferredFormat();

    int preferred_index = -1;
    for (int rate: device.supportedSampleRates()) {
        if (rate == preferred.sampleRate()) {
            preferred_index = ui.sample_rate->model()->rowCount();
        }
        ui.sample_rate->addItem(QString::number(rate), rate);
    }
    ui.sample_rate->setCurrentIndex(preferred_index);

    preferred_index = -1;
    for (int bits: device.supportedSampleSizes()) {
        if (bits == 24) {
            // we do not support 24 bit samples atm.
            continue;
        }
        if (bits == preferred.sampleSize()) {
            preferred_index = ui.sample_size->model()->rowCount();
        }
        ui.sample_size->addItem(QString::number(bits), bits);
    }
    ui.sample_size->setCurrentIndex(preferred_index);

    preferred_index = -1;
    for (QAudioFormat::SampleType type: device.supportedSampleTypes()) {
        bool skip = false;
        switch (type) {
        case QAudioFormat::Float: {
            ui.sample_type->addItem("float", type);
            break;
        }
        case QAudioFormat::SignedInt: {
            ui.sample_type->addItem("signed integer", type);
            break;
        }
        case QAudioFormat::UnSignedInt: {
            ui.sample_type->addItem("unsigned integer", type);
            break;
        }
        default:
            skip = true;
        }

        if (!skip) {
            if (type == preferred.sampleType()) {
                preferred_index = ui.sample_type->model()->rowCount()-1;
            }
        }
    }
    ui.sample_type->setCurrentIndex(preferred_index);

    ui.allow_stereo->setChecked(preferred.channelCount() > 1);

    update_buttons();
}

void OpenAudioDeviceDialog::on_sample_rate_currentIndexChanged(int)
{
    update_buttons();
}

void OpenAudioDeviceDialog::on_sample_size_currentIndexChanged(int)
{
    update_buttons();
}

void OpenAudioDeviceDialog::on_sample_type_currentIndexChanged(int)
{
    update_buttons();
}

void OpenAudioDeviceDialog::accept()
{
    QModelIndex current_device_index = ui.device_list->selectionModel()->currentIndex();
    if (!current_device_index.isValid() or
            ui.sample_rate->currentIndex() < 0 or
            ui.sample_size->currentIndex() < 0 or
            ui.sample_type->currentIndex() < 0)
    {
        return;
    }

    m_selected_device = m_devices[current_device_index.row()];
    m_selected_format = m_selected_device.preferredFormat();
    m_selected_format.setByteOrder(QAudioFormat::LittleEndian);
    m_selected_format.setCodec("audio/pcm");
    m_selected_format.setChannelCount(m_selected_format.channelCount() > 1 && ui.allow_stereo->checkState() == Qt::Checked ? 2 : 1);
    m_selected_format.setSampleRate(ui.sample_rate->currentData().toInt());
    m_selected_format.setSampleSize(ui.sample_size->currentData().toInt());
    m_selected_format.setSampleType((QAudioFormat::SampleType)ui.sample_type->currentData().toInt());

    if (!m_selected_device.isFormatSupported(m_selected_format)) {
        QMessageBox::critical(this, "Format not supported",
                              "The selected format is not supported by the selected device.",
                              QMessageBox::Ok, QMessageBox::NoButton);
        return;
    }

    QDialog::accept();
}
