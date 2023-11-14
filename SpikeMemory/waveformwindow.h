#ifndef WAVEFORMWINDOW_H
#define WAVEFORMWINDOW_H

#include <QDialog>
#include "qcustomplot.h"
#include "spikevm.h"

namespace Ui {
class WaveformWindow;
}

class WaveformWindow : public QDialog
{
    Q_OBJECT

public:
    explicit WaveformWindow(SpikeVM *spikeVM, QWidget *parent = nullptr);
    ~WaveformWindow();
    SpikeVM *spikeVM;
    void refreshDisplay();
    void clearPlots();

private slots:
    void startstop_button_toggled();
    void ch_range_changed();

private:
    Ui::WaveformWindow *ui;
    bool plotting = false;
    QVector<QCustomPlot*> plots;
    const int waveforms_per_plot = 20;
    int graph_cycler = 0;
};

#endif // WAVEFORMWINDOW_H
