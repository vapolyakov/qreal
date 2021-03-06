#include <QCoreApplication>
#include <QtWidgets/QAction>

#include "interpreter.h"

#include "details/autoconfigurer.h"
#include "details/robotImplementations/unrealRobotModelImplementation.h"
#include "details/robotCommunication/bluetoothRobotCommunicationThread.h"
#include "details/robotCommunication/usbRobotCommunicationThread.h"
#include "details/tracer.h"
#include "details/debugHelper.h"

using namespace qReal;
using namespace interpreters::robots;
using namespace interpreters::robots::details;

const Id startingElementType = Id("RobotsMetamodel", "RobotsDiagram", "InitialNode");
const Id startingElementType1 = Id("RobotsMetamodel", "RobotsDiagram", "InitialBlock");

Interpreter::Interpreter()
	: mGraphicalModelApi(NULL)
	, mLogicalModelApi(NULL)
	, mInterpretersInterface(NULL)
	, mState(idle)
	, mRobotModel(new RobotModel())
	, mBlocksTable(NULL)
	, mRobotCommunication(new RobotCommunicator(SettingsManager::value("valueOfCommunication").toString()))
	, mImplementationType(robotModelType::null)
	, mWatchListWindow(NULL)
	, mActionConnectToRobot(NULL)
{
	Tracer::enableAll();
	Tracer::setTarget(tracer::logFile);
	mParser = NULL;
	mBlocksTable = NULL;
	mTimer = new QTimer();

	mD2RobotModel = new d2Model::D2RobotModel();
	mD2ModelWidget = mD2RobotModel->createModelWidget();

	connect(mD2ModelWidget, SIGNAL(modelChanged(QDomDocument)), this, SLOT(on2dModelChanged(QDomDocument)));
	connect(mD2ModelWidget, SIGNAL(noiseSettingsChanged()), this, SIGNAL(noiseSettingsChangedBy2DModelWidget()));
	connect(this, SIGNAL(noiseSettingsChanged()), mD2ModelWidget, SLOT(rereadNoiseSettings()));
	connect(mRobotModel, SIGNAL(disconnected()), this, SLOT(disconnectSlot()));
	connect(mRobotModel, SIGNAL(sensorsConfigured()), this, SLOT(sensorsConfiguredSlot()));
	connect(mRobotModel, SIGNAL(connected(bool)), this, SLOT(connectedSlot(bool)));
	connect(mD2ModelWidget, SIGNAL(d2WasClosed()), this, SLOT(stopRobot()));
	connect(mRobotCommunication, SIGNAL(errorOccured(QString)), this, SLOT(reportError(QString)));
}

void Interpreter::init(GraphicalModelAssistInterface &graphicalModelApi
	, LogicalModelAssistInterface const &logicalModelApi
	, qReal::gui::MainWindowInterpretersInterface &interpretersInterface
	, qReal::ProjectManagementInterface const &projectManager)
{
	mGraphicalModelApi = &graphicalModelApi;
	mLogicalModelApi = &logicalModelApi;
	mInterpretersInterface = &interpretersInterface;

	mParser = new RobotsBlockParser(mInterpretersInterface->errorReporter());
	mBlocksTable = new BlocksTable(graphicalModelApi, logicalModelApi, mRobotModel, mInterpretersInterface->errorReporter(), mParser);

	connect(&projectManager, SIGNAL(beforeOpen(QString)), this, SLOT(stopRobot()));

	robotModelType::robotModelTypeEnum const modelType = static_cast<robotModelType::robotModelTypeEnum>(SettingsManager::value("robotModel").toInt());
	Tracer::debug(tracer::initialization, "Interpreter::init", "Going to set robot implementation, model type is " + DebugHelper::toString(modelType));
	setRobotImplementation(modelType);

	mWatchListWindow = new utils::WatchListWindow(mParser, mInterpretersInterface->windowWidget());
}

Interpreter::~Interpreter()
{
	foreach (Thread * const thread, mThreads) {
		delete thread;
	}
	delete mBlocksTable;
}

void Interpreter::interpret()
{
	Tracer::debug(tracer::initialization, "Interpreter::interpret", "Preparing for interpretation");

	mInterpretersInterface->errorReporter()->clear();

	Id const &currentDiagramId = mInterpretersInterface->activeDiagram();

	if (!mConnected) {
		mInterpretersInterface->errorReporter()->addInformation(tr("No connection to robot"));
		return;
	}
	if (mState != idle) {
		mInterpretersInterface->errorReporter()->addInformation(tr("Interpreter is already running"));
		return;
	}

	mBlocksTable->clear();
	mState = waitingForSensorsConfiguredToLaunch;
	mBlocksTable->setIdleForBlocks();

	Id const startingElement = findStartingElement(currentDiagramId);
	if (startingElement == Id()) {
		mInterpretersInterface->errorReporter()->addError(tr("No entry point found, please add Initial Node to a diagram"));
		mState = idle;
		return;
	}

	Autoconfigurer configurer(*mGraphicalModelApi, mBlocksTable, mInterpretersInterface->errorReporter(), mRobotModel);
	if (!configurer.configure(currentDiagramId)) {
		return;
	}
}

void Interpreter::stopRobot()
{
	mTimer->stop();
	mRobotModel->stopRobot();
	mState = idle;
	foreach (Thread *thread, mThreads) {
		delete thread;
		mThreads.removeAll(thread);
	}
	mBlocksTable->setFailure();
}

void Interpreter::showWatchList()
{
	mWatchListWindow->show();
}

void Interpreter::onTabChanged(Id const &diagramId, bool enabled)
{
	if (enabled) {
		Id const logicalId = mGraphicalModelApi->logicalId(diagramId);
		QString const xml = mGraphicalModelApi->property(logicalId, "worldModel").toString();
		QDomDocument worldModel;
		worldModel.setContent(xml);
		mD2ModelWidget->loadXml(worldModel);
		loadSensorConfiguration(logicalId);
		enableD2ModelWidgetRunStopButtons();
	} else {
		mD2ModelWidget->loadXml(QDomDocument());
		disableD2ModelWidgetRunStopButtons();
	}
}

void Interpreter::closeWatchList()
{
	if (mWatchListWindow) {
		mWatchListWindow->setVisible(false);
	}
}

void Interpreter::closeD2ModelWidget()
{
	if (mD2ModelWidget) {
		mD2ModelWidget->close();
	}
}

void Interpreter::showD2ModelWidget(bool isVisible)
{
	mD2ModelWidget->init(isVisible);
	if (isVisible) {
		mD2ModelWidget->activateWindow();
		mD2ModelWidget->showNormal();
	}
}

void Interpreter::setD2ModelWidgetActions(QAction *runAction, QAction *stopAction)
{
	mD2ModelWidget->setD2ModelWidgetActions(runAction, stopAction);
}

void Interpreter::enableD2ModelWidgetRunStopButtons()
{
	mD2ModelWidget->enableRunStopButtons();
}

void Interpreter::disableD2ModelWidgetRunStopButtons()
{
	mD2ModelWidget->disableRunStopButtons();
}

void Interpreter::setRobotImplementation(robotModelType::robotModelTypeEnum implementationType)
{
	mConnected = false;
	robotImplementations::AbstractRobotModelImplementation *robotImpl =
			robotImplementations::AbstractRobotModelImplementation::robotModel(implementationType, mRobotCommunication, mD2RobotModel);
	setRobotImplementation(robotImpl);
	mImplementationType = implementationType;
	if (mImplementationType != robotModelType::real) {
		mRobotModel->init();
	}
}

void Interpreter::connectedSlot(bool success)
{
	if (success) {
		if (mRobotModel->needsConnection()) {
			mInterpretersInterface->errorReporter()->addInformation(tr("Connected successfully"));
		}
	} else {
		Tracer::debug(tracer::initialization, "Interpreter::connectedSlot", "Robot connection status: " + QString::number(success));
		mInterpretersInterface->errorReporter()->addError(tr("Can't connect to a robot."));
	}
	mConnected = success;
	mActionConnectToRobot->setChecked(success);
}

void Interpreter::sensorsConfiguredSlot()
{
	Tracer::debug(tracer::initialization, "Interpreter::sensorsConfiguredSlot", "Sensors are configured");

	mConnected = true;
	mActionConnectToRobot->setChecked(mConnected);

	resetVariables();

	mRobotModel->nextBlockAfterInitial(mConnected);

	if (mState == waitingForSensorsConfiguredToLaunch) {
		mState = interpreting;

		runTimer();

		Tracer::debug(tracer::initialization, "Interpreter::sensorsConfiguredSlot", "Starting interpretation");
		mRobotModel->startInterpretation();

		Id const &currentDiagramId = mInterpretersInterface->activeDiagram();
		Id const startingElement = findStartingElement(currentDiagramId);
		Thread * const initialThread = new Thread(*mInterpretersInterface, *mBlocksTable, startingElement);
		addThread(initialThread);
	}
}

Id const Interpreter::findStartingElement(Id const &diagram) const
{
	IdList const children = mGraphicalModelApi->graphicalRepoApi().children(diagram);

	foreach (Id const child, children) {
		if (child.type() == startingElementType || child.type() == startingElementType1) {
			return child;
		}
	}

	return Id();
}

void Interpreter::threadStopped()
{
	Thread *thread = static_cast<Thread *>(sender());

	mThreads.removeAll(thread);
	delete thread;

	if (mThreads.isEmpty()) {
		stopRobot();
	}
}

void Interpreter::newThread(details::blocks::Block * const startBlock)
{
	Thread * const thread = new Thread(*mInterpretersInterface, *mBlocksTable, startBlock->id());
	addThread(thread);
}

void Interpreter::configureSensors(sensorType::SensorTypeEnum const &port1
		, sensorType::SensorTypeEnum const &port2
		, sensorType::SensorTypeEnum const &port3
		, sensorType::SensorTypeEnum const &port4)
{
	if (mConnected) {
		mRobotModel->configureSensors(port1, port2, port3, port4);
	}
}

void Interpreter::addThread(details::Thread * const thread)
{
	mThreads.append(thread);
	connect(thread, SIGNAL(stopped()), this, SLOT(threadStopped()));
	connect(thread, SIGNAL(newThread(details::blocks::Block*const)), this, SLOT(newThread(details::blocks::Block*const)));

	QCoreApplication::processEvents();
	thread->interpret();
}

interpreters::robots::details::RobotModel *Interpreter::robotModel()
{
	return mRobotModel;
}

void Interpreter::setRobotModel(details::RobotModel * const robotModel)
{
	mRobotModel = robotModel;
}

void Interpreter::setRobotImplementation(details::robotImplementations::AbstractRobotModelImplementation *robotImpl)
{
	mRobotModel->setRobotImplementation(robotImpl);
	if (robotImpl) {
		connect(mRobotModel, SIGNAL(connected(bool)), this, SLOT(runTimer()));
	}
}

void Interpreter::runTimer()
{
	if (mRobotModel->sensor(inputPort::port1)) {
		connect(mRobotModel->sensor(inputPort::port1)->sensorImpl(), SIGNAL(response(int)), this, SLOT(responseSlot1(int)), Qt::UniqueConnection);
		connect(mRobotModel->sensor(inputPort::port1)->sensorImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	}
	if (mRobotModel->sensor(inputPort::port2)) {
		connect(mRobotModel->sensor(inputPort::port2)->sensorImpl(), SIGNAL(response(int)), this, SLOT(responseSlot2(int)), Qt::UniqueConnection);
		connect(mRobotModel->sensor(inputPort::port2)->sensorImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	}
	if (mRobotModel->sensor(inputPort::port3)) {
		connect(mRobotModel->sensor(inputPort::port3)->sensorImpl(), SIGNAL(response(int)), this, SLOT(responseSlot3(int)), Qt::UniqueConnection);
		connect(mRobotModel->sensor(inputPort::port3)->sensorImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	}
	if (mRobotModel->sensor(inputPort::port4)) {
		connect(mRobotModel->sensor(inputPort::port4)->sensorImpl(), SIGNAL(response(int)), this, SLOT(responseSlot4(int)), Qt::UniqueConnection);
		connect(mRobotModel->sensor(inputPort::port4)->sensorImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	}

	connect(mRobotModel->encoderA().encoderImpl(), SIGNAL(response(int)), this, SLOT(responseSlotA(int)), Qt::UniqueConnection);
	connect(mRobotModel->encoderA().encoderImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	connect(mRobotModel->encoderB().encoderImpl(), SIGNAL(response(int)), this, SLOT(responseSlotB(int)), Qt::UniqueConnection);
	connect(mRobotModel->encoderB().encoderImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);
	connect(mRobotModel->encoderC().encoderImpl(), SIGNAL(response(int)), this, SLOT(responseSlotC(int)), Qt::UniqueConnection);
	connect(mRobotModel->encoderC().encoderImpl(), SIGNAL(failure()), this, SLOT(slotFailure()), Qt::UniqueConnection);

	mRobotModel->nullifySensors();
	if (!mTimer->isActive()) {
		readSensorValues();
		mTimer->start(25);
		connect(mTimer, SIGNAL(timeout()), this, SLOT(readSensorValues()), Qt::UniqueConnection);
	}
}

void Interpreter::readSensorValues()
{
	if (mState == idle) {
		return;
	}

	if (mRobotModel->sensor(inputPort::port1)) {
		mRobotModel->sensor(inputPort::port1)->read();
	}
	if (mRobotModel->sensor(inputPort::port2)) {
		mRobotModel->sensor(inputPort::port2)->read();
	}
	if (mRobotModel->sensor(inputPort::port3)) {
		mRobotModel->sensor(inputPort::port3)->read();
	}
	if (mRobotModel->sensor(inputPort::port4)) {
		mRobotModel->sensor(inputPort::port4)->read();
	}

	mRobotModel->encoderA().read();
	mRobotModel->encoderB().read();
	mRobotModel->encoderC().read();
}

void Interpreter::slotFailure()
{
	Tracer::debug(tracer::autoupdatedSensorValues, "Interpreter::slotFailure", "");
}

void Interpreter::responseSlot1(int sensorValue)
{
	updateSensorValues("Sensor1", sensorValue);
}

void Interpreter::responseSlot2(int sensorValue)
{
	updateSensorValues("Sensor2", sensorValue);
}

void Interpreter::responseSlot3(int sensorValue)
{
	updateSensorValues("Sensor3", sensorValue);
}

void Interpreter::responseSlot4(int sensorValue)
{
	updateSensorValues("Sensor4", sensorValue);
}

void Interpreter::responseSlotA(int encoderValue)
{
	updateSensorValues("EncoderA", encoderValue);
}

void Interpreter::responseSlotB(int encoderValue)
{
	updateSensorValues("EncoderB", encoderValue);
}

void Interpreter::responseSlotC(int encoderValue)
{
	updateSensorValues("EncoderC", encoderValue);
}

void Interpreter::updateSensorValues(QString const &sensorVariableName, int sensorValue)
{
	(*(mParser->getVariables()))[sensorVariableName] = utils::Number(sensorValue, utils::Number::intType);
	Tracer::debug(tracer::autoupdatedSensorValues, "Interpreter::updateSensorValues", sensorVariableName + QString::number(sensorValue));
}

void Interpreter::resetVariables()
{
	int const resetValue = 0;
	responseSlot1(resetValue);
	responseSlot2(resetValue);
	responseSlot3(resetValue);
	responseSlot4(resetValue);
}

void Interpreter::connectToRobot()
{
	if (mState == interpreting) {
		return;
	}
	if (mConnected) {
		mRobotModel->stopRobot();
		mRobotModel->disconnectFromRobot();
	} else {
		mRobotModel->init();
		configureSensors(static_cast<sensorType::SensorTypeEnum>(SettingsManager::instance()->value("port1SensorType").toInt())
						 , static_cast<sensorType::SensorTypeEnum>(SettingsManager::instance()->value("port2SensorType").toInt())
						 , static_cast<sensorType::SensorTypeEnum>(SettingsManager::instance()->value("port3SensorType").toInt())
						 , static_cast<sensorType::SensorTypeEnum>(SettingsManager::instance()->value("port4SensorType").toInt()));
	}
	mActionConnectToRobot->setChecked(mConnected);
}

void Interpreter::disconnectSlot()
{
	mActionConnectToRobot->setChecked(false);
	mConnected = false;
}

void Interpreter::setRobotModelType(robotModelType::robotModelTypeEnum robotModelType)
{
	setRobotImplementation(robotModelType);
}

void Interpreter::setCommunicator(QString const &valueOfCommunication, QString const &portName)
{
	if (valueOfCommunication == mLastCommunicationValue) {
		return;
	}
	RobotCommunicationThreadInterface *communicator = NULL;
	if (valueOfCommunication == "bluetooth") {
		communicator = new BluetoothRobotCommunicationThread();
	} else {
		communicator = new UsbRobotCommunicationThread();
	}
	mLastCommunicationValue = valueOfCommunication;

	mRobotCommunication->setRobotCommunicationThreadObject(communicator);
	mRobotCommunication->setPortName(portName);
}

void Interpreter::setConnectRobotAction(QAction *actionConnect)
{
	mActionConnectToRobot = actionConnect;
}

void Interpreter::setNoiseSettings()
{
	mD2RobotModel->setNoiseSettings();
	emit noiseSettingsChanged();
}

void Interpreter::reportError(QString const &message)
{
	mInterpretersInterface->errorReporter()->addError(message);
}

void Interpreter::on2dModelChanged(QDomDocument const &xml)
{
	Id const currentDiagramId = mInterpretersInterface->activeDiagram();
	Id const logicalId = mGraphicalModelApi->logicalId(currentDiagramId);
	if (logicalId != Id() && logicalId != Id::rootId()) {
		mGraphicalModelApi->setProperty(logicalId, "worldModel", xml.toString(4));
	}
}

void Interpreter::saveSensorConfiguration()
{
	Id const currentDiagramId = mInterpretersInterface->activeDiagram();
	Id const logicalId = mGraphicalModelApi->logicalId(currentDiagramId);
	if (logicalId != Id() && logicalId != Id::rootId()) {
		int const sensor1Value = SettingsManager::value("port1SensorType").toInt();
		int const sensor2Value = SettingsManager::value("port2SensorType").toInt();
		int const sensor3Value = SettingsManager::value("port3SensorType").toInt();
		int const sensor4Value = SettingsManager::value("port4SensorType").toInt();
		mGraphicalModelApi->setProperty(logicalId, "sensor1Value", QString::number(sensor1Value));
		mGraphicalModelApi->setProperty(logicalId, "sensor2Value", QString::number(sensor2Value));
		mGraphicalModelApi->setProperty(logicalId, "sensor3Value", QString::number(sensor3Value));
		mGraphicalModelApi->setProperty(logicalId, "sensor4Value", QString::number(sensor4Value));
	}
}

void Interpreter::loadSensorConfiguration(Id const &diagramId)
{
	int const oldSensor1Value = SettingsManager::value("port1SensorType").toInt();
	int const oldSensor2Value = SettingsManager::value("port2SensorType").toInt();
	int const oldSensor3Value = SettingsManager::value("port3SensorType").toInt();
	int const oldSensor4Value = SettingsManager::value("port4SensorType").toInt();

	int const sensor1Value = mGraphicalModelApi->property(diagramId, "sensor1Value").toInt();
	int const sensor2Value = mGraphicalModelApi->property(diagramId, "sensor2Value").toInt();
	int const sensor3Value = mGraphicalModelApi->property(diagramId, "sensor3Value").toInt();
	int const sensor4Value = mGraphicalModelApi->property(diagramId, "sensor4Value").toInt();

	bool const somethingChanged = oldSensor1Value != sensor1Value
			|| oldSensor2Value != sensor2Value
			|| oldSensor3Value != sensor3Value
			|| oldSensor4Value != sensor4Value;

	SettingsManager::setValue("port1SensorType", sensor1Value);
	SettingsManager::setValue("port2SensorType", sensor2Value);
	SettingsManager::setValue("port3SensorType", sensor3Value);
	SettingsManager::setValue("port4SensorType", sensor4Value);

	if (somethingChanged) {
		emit sensorsConfigurationChanged();
	}
}

utils::WatchListWindow *Interpreter::watchWindow() const
{
	return mWatchListWindow;
}

void Interpreter::connectSensorConfigurer(details::SensorsConfigurationWidget *configurer) const
{
	connect(configurer, SIGNAL(saved()), mD2ModelWidget, SLOT(syncronizeSensors()));
}
