#ifndef RASTERWINDOW_H
#define RASTERWINDOW_H

#include <QDialog>
#include "qcustomplot.h"
#include "spikevm.h"

namespace Ui {
class RasterWindow;
}

class RasterWindow : public QDialog
{
    Q_OBJECT

public:
    explicit RasterWindow(SpikeVM *spikeVM, QWidget *parent = nullptr);
    ~RasterWindow();
    SpikeVM *spikeVM;
    void refreshDisplay();
    void clearPlots();
    void updatePlotData();
    std::vector<int> getEventSubtypeIndices();

private slots:
    void startstop_button_toggled();
    void ch_range_changed();

    void on_event_family_comboBox_currentTextChanged(const QString &arg1);

private:
    Ui::RasterWindow *ui;
    bool plotting = false;
    QVector<QCustomPlot*> plots;
};

#endif // RASTERWINDOW_H
