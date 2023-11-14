#include "waveformwindow.h"
#include "ui_waveformwindow.h"

WaveformWindow::WaveformWindow(SpikeVM *spikeVM, QWidget *parent) :
    QDialog(parent),
    spikeVM(spikeVM),
    ui(new Ui::WaveformWindow)
{
    ui->setupUi(this);
    //restrict probe ind spinbox to valid values
    ui->probe_ind_spinBox->setMinimum(0);
    ui->probe_ind_spinBox->setMaximum(spikeVM->num_probes - 1);
    ui->ch_range_spinbox->setMinimum(0);
    ui->ch_range_spinbox->setMaximum(spikeVM->imec_fetch_containers[0]->n_cs - 1);
    ui->ch_range_spinbox->setValue(0);
    ui->ch_range_spinbox->setSingleStep(32);
    const int pen_width = 1;

    //iterate over the rows of plots:
    QGridLayout* plot_layout = qobject_cast<QGridLayout*>(ui->plot_frame->layout());
    if (plot_layout) {
        int numRows = plot_layout->rowCount();
        int numCols = plot_layout->columnCount();
        for (int row = 0; row < numRows; ++row){
            for (int col = 0; col < numCols; ++col){
                QLayoutItem* item = plot_layout->itemAtPosition(row, col);
                if (item) {
                    QWidget* child_widget = item->widget();
                    QCustomPlot* child_plot = qobject_cast<QCustomPlot*>(child_widget);
                    plots.append(child_plot);
                    for (int graph_ind = 0; graph_ind < waveforms_per_plot; ++graph_ind) {
                        child_plot->addGraph();
                        //Our intraop monitor is too dim for the nice colors :(
//                        child_plot->graph(graph_ind)->setPen(QPen(Qt::darkMagenta));
                        child_plot->graph(graph_ind)->setPen(QPen(Qt::black, pen_width));
                        child_plot->graph(graph_ind)->setScatterStyle(QCPScatterStyle::ssNone);
                        child_plot->graph(graph_ind)->setLineStyle(QCPGraph::lsLine);
                    }
                    child_plot->yAxis->setRange(-150,150);
                    child_plot->xAxis->setRange(-23, 23);
                    child_plot->xAxis->grid()->setVisible(false);
                    child_plot->yAxis->grid()->setVisible(false);
                    child_plot->xAxis->setVisible(false);
                    child_plot->yAxis->setVisible(false);
                    child_plot->xAxis->setTickLabels(false);
                    child_plot->yAxis->setTickLabels(false);
                    child_plot->setBackground(Qt::white);
                    //Monitor is too low-contrast for the dark background too..
//                    child_plot->setBackground(QColor(44,44,48));
                }
            }
        }
    }
    connect(ui->waveformWindow_startStop_button, &QPushButton::clicked, this, &WaveformWindow::startstop_button_toggled);
//    connect(ui->spinBox, &QSpinBox::valueChanged, this, &WaveformWindow::ch_range_changed);
    QTimer *display_timer = new QTimer(this);
    connect(display_timer, &QTimer::timeout, this, QOverload<>::of(&WaveformWindow::refreshDisplay));
    //    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MainWindow::refreshDisplay));
    display_timer->start(50);
}

WaveformWindow::~WaveformWindow()
{
    delete ui;
}

void WaveformWindow::refreshDisplay()
{

    if(!plotting){
        return;
    }

    int probe_ind = ui->probe_ind_spinBox->value();
    for (int i = 0; i < plots.size(); ++i) {
        int ch = spikeVM->channel_maps[probe_ind][i + ui->ch_range_spinbox->value()];
        if( spikeVM->waveform_x[probe_ind].empty() || spikeVM->waveform_x[probe_ind][ch][0] == 0){
            plots[i]->graph(graph_cycler)->data()->clear();
        }
        else{
            plots[i]->graph(graph_cycler)->setData(spikeVM->waveform_x[probe_ind][ch], spikeVM->waveform_y[probe_ind][ch]);
            spikeVM->waveform_x[probe_ind][ch] = QVector<double>(32, 0);
            spikeVM->waveform_y[probe_ind][ch] = QVector<double>(32, 0);
        }
        plots[i]->replot();
        plots[i]->update();
    }
    graph_cycler++;
    graph_cycler = graph_cycler % waveforms_per_plot;
}

void WaveformWindow::clearPlots()
{
    for (int i = 0; i < plots.size(); ++i) {
        for (int j = 0; j< waveforms_per_plot; ++j){
            plots[i]->graph(graph_cycler)->data()->clear();
        }
        plots[i]->replot();
        plots[i]->update();
    }
}

void WaveformWindow::startstop_button_toggled()
{
    if(plotting){
        ui->waveformWindow_startStop_button->setText("Start");
    } else {
        ui->waveformWindow_startStop_button->setText("Stop");
    }
    plotting = !plotting;
    qDebug() << plotting;
}

void WaveformWindow::ch_range_changed()
{
    clearPlots();
}

