#include "spikemap.h"
#include "ui_spikemap.h"

SpikeMap::SpikeMap(SpikeVM *spikeVM, QWidget *parent) :
    QDialog(parent),
    spikeVM(spikeVM),
    ui(new Ui::SpikeMap)
{
    ui->setupUi(this);
    ui->probe_ind_spinBox->setMinimum(0);
    ui->probe_ind_spinBox->setMaximum(spikeVM->num_probes - 1);
    ui->lead_chan_spinBox->setMinimum(0);
    ui->n_cs_spinBox->setMinimum(1);
    ui->n_cs_spinBox->setMaximum(spikeVM->imec_fetch_containers[0]->n_cs);
    ui->n_cs_spinBox->setValue(spikeVM->imec_fetch_containers[ui->probe_ind_spinBox->value()]->n_cs);
    ui->local_spinBox->setMinimum(1);
    ui->local_spinBox->setValue(60);
    ui->local_spinBox->setMaximum(300);
    ui->bin_width_ms_spinBox->setMinimum(100);
    ui->bin_width_ms_spinBox->setMaximum(5000);
    ui->bin_width_ms_spinBox->setValue(300);
    ui->global_start_spinBox->setMinimum(0);
    ui->global_start_spinBox->setValue(0);
    spikeHeatmap = new QCPColorMap(ui->spikeMap_qcp->xAxis, ui->spikeMap_qcp->yAxis);
    QCPColorGradient gradient;
    gradient.loadPreset(QCPColorGradient::gpThermal);
    spikeHeatmap->setGradient(gradient);
    ui->spikeMap_qcp->setBackground(QColor(44,44,48));
    ui->spikeMap_startStop_button->setText("Start");
    connect(ui->spikeMap_startStop_button, &QPushButton::clicked, this, &SpikeMap::startstop_button_toggled);
    QTimer *display_timer = new QTimer(this);
    connect(display_timer, &QTimer::timeout, this, QOverload<>::of(&SpikeMap::refreshDisplay));
    //    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MainWindow::refreshDisplay));

    initializeSpikeBins();
    display_timer->start(300);
}

SpikeMap::~SpikeMap()
{
    delete ui;
}


void SpikeMap::initializeSpikeBins()
{
    //initialize last_binned_spike_inds, binned_spike_counts, and bin_edges
    last_binned_spike_inds = QVector<QVector<t_ull>>(spikeVM->num_probes);
    binned_spike_counts = QVector<QVector<QVector<double>>>(spikeVM->num_probes);

    //Find the earliest spike time across all probes and channels
    earliest_spike_time = std::numeric_limits<t_ull>::max();
    for(int probe_ind = 0; probe_ind < spikeVM->num_probes; ++probe_ind){
        for(int ch = 0; ch < spikeVM->imec_fetch_containers[probe_ind]->n_cs; ++ch){
            if(spikeVM->spike_times_ms[probe_ind][ch].size() > 0){
                earliest_spike_time = std::min(earliest_spike_time, spikeVM->spike_times_ms[probe_ind][ch][0]);
            }
        }
    }

    //Set the first bin edge to the earliest spike time
//    bin_edges = QVector<double>(1, earliest_spike_time - 1);
    bin_edges = QVector<double>(1, 0);
    for(int probe_ind = 0; probe_ind < spikeVM->num_probes; ++probe_ind){
        last_binned_spike_inds[probe_ind] = QVector<t_ull>(spikeVM->imec_fetch_containers[probe_ind]->n_cs, 0);
        binned_spike_counts[probe_ind] = QVector<QVector<double>>(spikeVM->imec_fetch_containers[probe_ind]->n_cs);
        for(int ch = 0; ch < spikeVM->imec_fetch_containers[probe_ind]->n_cs; ++ch){
            binned_spike_counts[probe_ind][ch] = QVector<double>(1, 0);
        }
    }
}

void SpikeMap::prepareSpikeHeatmap()
{
    t_ull n_x_bins = 0;
    t_ull start_bin_ind = 0;
    int probe_ind = ui->probe_ind_spinBox->value();
    int lead_ch = ui->lead_chan_spinBox->value();
    int n_cs = std::min(ui->n_cs_spinBox->value(), spikeVM->imec_fetch_containers[probe_ind]->n_cs - lead_ch + 1);

    //Determine the range of bins to plot and the initial bin index
    if(global_mode){
//        start_bin_ind = earliest_spike_time / bin_width_ms;
        start_bin_ind = 0;
        n_x_bins = bin_edges.size() - start_bin_ind;
    }
    else{
        n_x_bins = local_history_duration_ms / bin_width_ms;
        start_bin_ind = bin_edges.size() - n_x_bins;
    }

    //Set the range of the heatmap
    spikeHeatmap->data()->setSize(n_x_bins, n_cs);
    spikeHeatmap->data()->setRange(QCPRange(0, n_x_bins), QCPRange(0, n_cs));

    //Iterate over all bins and channels and set the heatmap data
    for(t_ull bin_ind = start_bin_ind; bin_ind < start_bin_ind + n_x_bins; ++bin_ind){
        for(int ch = 0; ch < n_cs; ++ch){
            spikeHeatmap->data()->setCell(bin_ind - start_bin_ind, ch, binned_spike_counts[probe_ind][spikeVM->channel_maps[probe_ind][lead_ch + ch]][bin_ind]);
        }
    }

    //Rescale the heatmap to the data
    spikeHeatmap->rescaleDataRange();

    //Turn on interpolation for the heatmap
    spikeHeatmap->setInterpolate(true);
}

void SpikeMap::updateSpikeBins(){
    //Iterate over all probes and channels, and update the spike bins for each channel
    for(int probe_ind = 0; probe_ind < spikeVM->num_probes; ++probe_ind){
        for(int ch = 0; ch < spikeVM->imec_fetch_containers[probe_ind]->n_cs; ++ch){
            //Get the latest spike index for this channel
            t_ull latest_spike_ind = spikeVM->spike_times_ms[probe_ind][ch].size() - 1;
            //Get the last binned spike index for this channel
            t_ull last_binned_spike_ind = last_binned_spike_inds[probe_ind][ch];
            //Iterate over all spikes since the last binned spike
            for(t_ull spike_ind = last_binned_spike_ind; spike_ind <= latest_spike_ind; ++spike_ind){
                //Get the spike time
                t_ull spike_time = spikeVM->spike_times_ms[probe_ind][ch][spike_ind];
                //Check if a bin exists for this spike time
                if(spike_time > bin_edges.back()){
                    //Add new bin edges until the spike time is within the last bin
                    while(spike_time > bin_edges.back()){
                        bin_edges.push_back(bin_edges.back() + bin_width_ms);
                        //Add a new bin to each channel for this probe
                        for(int probe_ind_2 = 0; probe_ind_2 < spikeVM->num_probes; ++probe_ind_2){
                            for(int ch_2 = 0; ch_2 < spikeVM->imec_fetch_containers[probe_ind_2]->n_cs; ++ch_2){
                                binned_spike_counts[probe_ind_2][ch_2].push_back(0);
                            }
                        }
                    }
                }
                //Get the bin index for the spike time
                t_ull bin_ind = spike_time / bin_width_ms;

                //Increment the spike count for this bin
                binned_spike_counts[probe_ind][ch][bin_ind] += 1;
            }
            //Update the last binned spike index for this channel
            last_binned_spike_inds[probe_ind][ch] = latest_spike_ind + 1;
        }
    }

    ui->global_start_spinBox->setMaximum(((bin_edges.size() - 1) * bin_width_ms) / (60 * 1000));
}

void SpikeMap::refreshDisplay()
{
    if(bin_width_changed){
        bin_width_ms = ui->bin_width_ms_spinBox->value();
        initializeSpikeBins();
    }
    updateSpikeBins();
    if(plotting){
        //Prepare spike heatmap
        prepareSpikeHeatmap();

        //Rescale axes
        ui->spikeMap_qcp->rescaleAxes();

        //Replot
        ui->spikeMap_qcp->update();
        ui->spikeMap_qcp->replot();
    }
    else{
        return;
    }
}


void SpikeMap::startstop_button_toggled()
{
    if(plotting){
        ui->spikeMap_startStop_button->setText("Start");
    } else {
        ui->spikeMap_startStop_button->setText("Stop");
    }
    plotting = !plotting;
}

void SpikeMap::on_globalMap_radioButton_clicked()
{
    //Prepare switch to global mode
    global_mode = true;
}


void SpikeMap::on_recentMap_radioButton_clicked()
{
    global_mode = false;
}


void SpikeMap::on_local_spinBox_valueChanged(int arg1)
{
    local_history_duration_ms = 1000 * arg1;
}


void SpikeMap::on_bin_width_ms_spinBox_valueChanged(int arg1)
{
    bin_width_changed = true;
}


void SpikeMap::on_global_start_spinBox_valueChanged(int arg1)
{
    global_start_time_ms = 60 * 1000 * arg1;
}

