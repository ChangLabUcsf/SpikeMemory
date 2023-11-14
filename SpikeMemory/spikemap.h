#ifndef SPIKEMAP_H
#define SPIKEMAP_H

#include <QDialog>
#include "qcustomplot.h"
#include "spikevm.h"

namespace Ui {
class SpikeMap;
}

class SpikeMap : public QDialog
{
    Q_OBJECT

public:
    explicit SpikeMap(SpikeVM *spikeVM, QWidget *parent = nullptr);
    ~SpikeMap();
    SpikeVM *spikeVM;
    void refreshDisplay();
    bool global_mode = true;
    t_ull bin_width_ms = 300;
    t_ull local_history_duration_ms = 60 * 1000;

private slots:
    void startstop_button_toggled();

    void on_globalMap_radioButton_clicked();

    void on_recentMap_radioButton_clicked();

    void on_local_spinBox_valueChanged(int arg1);

    void on_bin_width_ms_spinBox_valueChanged(int arg1);

    void on_global_start_spinBox_valueChanged(int arg1);

private:
    Ui::SpikeMap *ui;
    void prepareSpikeHeatmap();
    void initializeSpikeBins();
    void updateSpikeBins();
//    SpikeVM *spikeVM;
    bool plotting = false;
    bool bin_width_changed = false;
    t_ull global_start_time_ms = 0;
    t_ull earliest_spike_time = 0;
    QVector<QVector<QVector<double>>> binned_spike_counts;
    QVector<double> bin_edges;
    QVector<QVector<t_ull>> last_binned_spike_inds;
    QCPColorMap *spikeHeatmap;
};

#endif // SPIKEMAP_H
