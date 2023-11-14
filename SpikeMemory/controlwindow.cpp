#include "controlwindow.h"
#include "ui_controlwindow.h"

ControlWindow::ControlWindow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ControlWindow)
    , settings("SpikeGLX", "SpikeGLX")
{
    ui->setupUi(this);

    //set port/ip parameters in ui to defaults
    restoreDefaultSettings();

    //initialize threshold display parameters
    ui->rms_threshold_radioButton->setChecked(true);

    ui->connection_status_label->setStyleSheet("QLabel { color : red; }");
    ui->connection_status_label->setText("SpikeGLX Not Connected");

    //connect threshold display slots to our custom slots
    QObject::connect(ui->absolute_threshold_doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ControlWindow::on_absolute_threshold_doubleSpinBox_valueChanged);
    QObject::connect(ui->rms_threshold_doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ControlWindow::on_rms_threshold_doubleSpinBox_valueChanged);

    //connect connect button to our custom slot
    QObject::connect(ui->connect_pushButton, &QPushButton::clicked, this, &ControlWindow::connect_button_clicked);

}

ControlWindow::~ControlWindow()
{
    delete ui;
}

void ControlWindow::restoreDefaultSettings()
{
    //load ip, port, threshold settings and set these in the ui
    QString lastIP = settings.value("lastIP", "").toString();
    ui->ip_lineEdit->setText(lastIP);
    QString lastPort = settings.value("lastPort", "").toString();
    ui->port_lineEdit->setText(lastPort);
    QString lastThreshold = settings.value("lastThreshold", "").toString();
    ui->absolute_threshold_doubleSpinBox->setValue(lastThreshold.toDouble());
    QString lastRMSThreshold = settings.value("lastRMSThreshold", "").toString();
    ui->rms_threshold_doubleSpinBox->setValue(lastRMSThreshold.toDouble());
}

void ControlWindow::saveDefaultSettings()
{
    settings.setValue("lastIP", ui->ip_lineEdit->text());
    settings.setValue("lastPort", ui->port_lineEdit->text());
    settings.setValue("lastThreshold", ui->absolute_threshold_doubleSpinBox->value());
    settings.setValue("lastRMSThreshold", ui->rms_threshold_doubleSpinBox->value());
    settings.sync();
}

void ControlWindow::initializeChildWindows()
{
    //create windows
    raster_window = new RasterWindow(spikeVM, this);
    spikemap_window = new SpikeMap(spikeVM,this);
    waveform_window = new WaveformWindow(spikeVM, this);

    //link window closure to corresponding controller slots
    QObject::connect(raster_window, &RasterWindow::finished, this, &ControlWindow::raster_window_closure);
    QObject::connect(spikemap_window, &SpikeMap::finished, this, &ControlWindow::spikemap_window_closure);
    QObject::connect(waveform_window, &WaveformWindow::finished, this, &ControlWindow::waveform_window_closure);
}


void ControlWindow::raster_window_closure()
{
    //uncheck the checkbox on the control panel
    ui->raster_window_cb->setChecked(false);
}

void ControlWindow::spikemap_window_closure()
{
    //uncheck the checkbox on the control panel
    ui->spikemap_window_cb->setChecked(false);
}

void ControlWindow::waveform_window_closure()
{
    //uncheck the checkbox on the control panel
    ui->waveform_window_cb->setChecked(false);
}

void ControlWindow::on_raster_window_cb_stateChanged(int arg1)
{
    //hide or show raster window ui, as appropriate
    if (arg1 == 0) {
        raster_window->hide();
    }
    else {
        //show this window, maximized
        raster_window->setWindowState(Qt::WindowMaximized);
        raster_window->show();
    }
    return;
}

void ControlWindow::on_spikemap_window_cb_stateChanged(int arg1)
{
    //hide or show spike map window, as appropriate
    if (arg1 == 0) {
        spikemap_window->hide();
    }
    else {
        //show this window, maximized
        spikemap_window->setWindowState(Qt::WindowMaximized);
        spikemap_window->show();
    }
    return;
}


void ControlWindow::on_waveform_window_cb_stateChanged(int arg1)
{
    //hide or show waveform window, as appropriate
    if (arg1 == 0) {
        waveform_window->hide();
    }
    else {
        //show this window, maximized
        waveform_window->setWindowState(Qt::WindowMaximized);
        waveform_window->show();
    }
    return;
}

void ControlWindow::on_absolute_threshold_doubleSpinBox_valueChanged(double arg1)
{
    //if spikeGLX is connected, update spikeVM queued threshold for it to change at the next cycle
    if(!connectionEstablished){
        return;
    }
    spikeVM->queued_absolute_threshold_imec = arg1;
}

void ControlWindow::on_rms_threshold_doubleSpinBox_valueChanged(double arg1)
{
    //if spikeGLX is connected, update spikeVM rms_threshold
    if(!connectionEstablished){
        return;
    }
    spikeVM->queued_rms_threshold_imec = arg1;
}

void ControlWindow::connect_button_clicked()
{

    if(!connectionEstablished){//establish connection to spikeGLX with current parameters
        //create spikevm
        spikeVM = new SpikeVM(this, ui->ip_lineEdit->text().toStdString().c_str(), ui->port_lineEdit->text().toInt());
        if(spikeVM->establishConnection()){
            initializeChildWindows();
            connectionEstablished = true;
            //change the button text to "disconnect"
            ui->connect_pushButton->setText("Disconnect");
            //change the status label text to green, with the ip/port
            ui->connection_status_label->setStyleSheet("QLabel { color : green; }");
            ui->connection_status_label->setText("SpikeGLX connected at:\n" + ui->ip_lineEdit->text() + ":" + ui->port_lineEdit->text());
            spikeVM->queued_absolute_threshold_imec = ui->absolute_threshold_doubleSpinBox->value();
            spikeVM->queued_rms_threshold_imec = ui->rms_threshold_doubleSpinBox->value();
            spikeVM->queued_RMS_based_spike_detection = ui->rms_threshold_radioButton->isChecked();
        }
        else{
            //delete this spikeVM so we can try again, e.g., possibly with a new address
            delete spikeVM;
        }
    }
    else{
        //Double check that the user wants to disconnect
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Disconnect?", "Are you sure you want to disconnect?", QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            //delete all child windows
            raster_window->close();
            spikemap_window->close();
            waveform_window->close();

            //delete the spikeVM QObject
            delete spikeVM;
            connectionEstablished = false;
            //change the button text to "connect"
            ui->connect_pushButton->setText("Connect");
            //change the status label text to red
            ui->connection_status_label->setStyleSheet("QLabel { color : red; }");
            ui->connection_status_label->setText("SpikeGLX Not Connected");
        }
    }
}

void ControlWindow::on_restoreDefaults_pushButton_clicked()
{
    //restore default settings in the ui
    restoreDefaultSettings();
}


void ControlWindow::on_saveDefaults_pushButton_clicked()
{
    //save current settings in the ui as defaults
    saveDefaultSettings();
}


void ControlWindow::on_absolute_threshold_radioButton_clicked()
{
    //if spikeGLX is connected, update spikeVM to use absolute threshold
    if(!connectionEstablished){
        return;
    }
    spikeVM->queued_RMS_based_spike_detection = false;
}


void ControlWindow::on_rms_threshold_radioButton_clicked()
{
    //if spikeGLX is connected, update spikeVM to use RMS threshold
    if(!connectionEstablished){
        return;
    }
    spikeVM->queued_RMS_based_spike_detection = true;
}

