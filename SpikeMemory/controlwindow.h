#ifndef CONTROLWINDOW_H
#define CONTROLWINDOW_H

#include <QDialog>
#include <QSettings>
#include "spikemap.h"
#include "rasterwindow.h"
#include "waveformwindow.h"
#include "spikevm.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ControlWindow; class SpikeMap; class RasterWindow; class WaveformWindow;}
QT_END_NAMESPACE

class ControlWindow : public QDialog
{
    Q_OBJECT

public:
    ControlWindow(QWidget *parent = nullptr);
    ~ControlWindow();

private slots:
    void raster_window_closure();

    void spikemap_window_closure();

    void waveform_window_closure();

    void on_raster_window_cb_stateChanged(int arg1);

    void on_spikemap_window_cb_stateChanged(int arg1);

    void on_waveform_window_cb_stateChanged(int arg1);

    void on_absolute_threshold_doubleSpinBox_valueChanged(double arg1);

    void on_rms_threshold_doubleSpinBox_valueChanged(double arg1);

    void connect_button_clicked();

    void on_restoreDefaults_pushButton_clicked();

    void on_saveDefaults_pushButton_clicked();

    void on_absolute_threshold_radioButton_clicked();

    void on_rms_threshold_radioButton_clicked();

private:
    void initializeChildWindows();
    void restoreDefaultSettings();
    void saveDefaultSettings();
    bool connectionEstablished = false;

    Ui::ControlWindow *ui;
    SpikeMap *spikemap_window;
    RasterWindow *raster_window;
    WaveformWindow *waveform_window;
    SpikeVM *spikeVM;
    QSettings settings;
};
#endif // CONTROLWINDOW_H
