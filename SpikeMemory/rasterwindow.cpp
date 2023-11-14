#include "rasterwindow.h"
#include "ui_rasterwindow.h"

RasterWindow::RasterWindow(SpikeVM *spikeVM, QWidget *parent) :
    QDialog(parent),
    spikeVM(spikeVM),
    ui(new Ui::RasterWindow)
{
    ui->setupUi(this);
    //restrict probe ind spinbox to valid values
    ui->probe_ind_spinBox->setMinimum(0);
    ui->probe_ind_spinBox->setMaximum(spikeVM->num_probes - 1);
    ui->event_type_spinBox->setMinimum(0);
    ui->event_type_spinBox->setMaximum(spikeVM->num_event_types - 1);
    //TODO: update this on a per-probe basis to keep the maximum appropriate
    ui->ch_range_spinbox->setMaximum(spikeVM->imec_fetch_containers[0]->n_cs - 1);
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
                    child_plot->addGraph();
//                    child_plot->graph(0)->setPen(QPen(Qt::darkMagenta, pen_width));
                    child_plot->graph(0)->setPen(QPen(Qt::black, pen_width));
                    child_plot->graph(0)->setScatterStyle(QCPScatterStyle::ssCircle);
                    child_plot->graph(0)->setLineStyle(QCPGraph::lsNone);
                    child_plot->yAxis->setRange(-1,10);
                    child_plot->xAxis->setRange(-2400, 2400);
                    child_plot->xAxis->grid()->setVisible(false);
                    child_plot->yAxis->grid()->setVisible(false);
//                    child_plot->yAxis->grid()->setSubGridVisible(false);
//                    child_plot->yAxis->grid()->setSubGridPen(QPen(Qt::NoPen));
//                    child_plot->yAxis->grid()->setZeroLinePen(QPen(Qt::black));
                    child_plot->xAxis->setVisible(false);
                    child_plot->yAxis->setVisible(false);
                    child_plot->xAxis->setTickLabels(false);
                    child_plot->yAxis->setTickLabels(false);
                    child_plot->setBackground(Qt::white);
//                    child_plot->setBackground(QColor(44,44,48));

                    //Add a vertical line at x=0
                    QCPItemStraightLine *zeroLine = new QCPItemStraightLine(child_plot);
                    zeroLine->point1->setCoords(0, -1);
                    zeroLine->point2->setCoords(0, 10);
                    zeroLine->setPen(QPen(Qt::black));
                    zeroLine->setLayer("background");
                }
            }
        }
    }
    connect(ui->startStop_button, &QPushButton::clicked, this, &RasterWindow::startstop_button_toggled);
    QTimer *display_timer = new QTimer(this);
    connect(display_timer, &QTimer::timeout, this, QOverload<>::of(&RasterWindow::refreshDisplay));
    //    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MainWindow::refreshDisplay));
    display_timer->start(500);
}

RasterWindow::~RasterWindow()
{
    delete ui;
}

void RasterWindow::refreshDisplay()
{

    if(!plotting){
        return;
    }

    updatePlotData();
    int probe_ind = ui->probe_ind_spinBox->value();
//    int event_type = ui->event_type_spinBox->value();
    int event_type = 9;
    bool resize = spikeVM->spikes_by_event[probe_ind][0][event_type].size() > plots[0]->yAxis->range().upper;

    for (int i = 0; i < plots.size(); ++i) {
        if(resize){
            //Update y-axis range
            plots[i]->yAxis->setRange(-.5, spikeVM->spikes_by_event[probe_ind][0][event_type].size());
        }
        int ch = i + ui->ch_range_spinbox->value();
        if(ch >= spikeVM->spikes_by_event[probe_ind].size()){
            plots[i]->graph(0)->setVisible(false);
            continue;
        }
        plots[i]->replot();
        plots[i]->update();
    }
}

void RasterWindow::updatePlotData(){
    int probe_ind = ui->probe_ind_spinBox->value();
//    int event_type = ui->event_type_spinBox->value();
    int event_type = 9;

    //Get the indices of the event subtypes we want to plot
    std::vector<int> event_subtype_indices = getEventSubtypeIndices();
    if(event_subtype_indices.size() == 0){
        //clear plots
        clearPlots();
        return;
    }
    for (int i = 0; i < plots.size(); ++i) {
        int ch = i + ui->ch_range_spinbox->value();
        if(ch >= spikeVM->spikes_by_event[probe_ind].size()){
            continue;
        }
//        qDebug() << "ch: " << ch;
        plots[i]->graph(0)->setVisible(true);
        QVector<double> plot_x = QVector<double>();
        QVector<double> plot_y = QVector<double>();
        for (int subtype_instance_ind = 0; subtype_instance_ind < event_subtype_indices.size(); subtype_instance_ind++){
            int instance_ind = event_subtype_indices[subtype_instance_ind];
//            qDebug() << "instance ind: " << instance_ind;
//            qDebug() << spikeVM->spikes_by_event[probe_ind][ch][event_type][instance_ind].size();
            for(int j = 0; j < spikeVM->spikes_by_event[probe_ind][ch][event_type][instance_ind].size(); ++j){
                plot_x.push_back(spikeVM->spikes_by_event[probe_ind][ch][event_type][instance_ind][j]);
                plot_y.push_back(subtype_instance_ind);
            }
        }
        plots[i]->graph(0)->setData(plot_x, plot_y);
    }
}

std::vector<int> RasterWindow::getEventSubtypeIndices(){
    std::vector<int> event_subtype_indices;
    QString event_family = ui->event_family_comboBox->currentText();
    QString event_subtype = ui->event_type_comboBox->currentText();

    //for TIMIT there is only one subtype available, so we just return the indices of all events on channel 9
    if(event_family == "TIMIT"){
        for(int i = 0; i < spikeVM->spikes_by_event[0][0][9].size(); ++i){
            event_subtype_indices.push_back(i);
        }
        return event_subtype_indices;
    }

    return event_subtype_indices;
}

void RasterWindow::clearPlots()
{
    for (int i = 0; i < plots.size(); ++i) {
        plots[i]->graph(0)->data()->clear();
        plots[i]->replot();
        plots[i]->update();
    }
}

void RasterWindow::startstop_button_toggled()
{
    if(plotting){
        ui->startStop_button->setText("Start");
    } else {
        ui->startStop_button->setText("Stop");
    }
    plotting = !plotting;
    qDebug() << plotting;
}

void RasterWindow::ch_range_changed()
{
    clearPlots();
}

void RasterWindow::on_event_family_comboBox_currentTextChanged(const QString &arg1)
{
    //Set the event subtype combobox to the appropriate values
    ui->event_type_comboBox->clear();
    if(arg1 == "TIMIT"){
        //TIMIT has only one event subtype, so we just add that to the combobox
        ui->event_type_comboBox->addItem("All");

        //and set the event_type combobox to have this selected
        ui->event_type_comboBox->setCurrentIndex(0);
        return;
    }

}

