#pragma once

#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QProcess>
#include <QtCore/QPair>
#include <QtCore/QHash>

namespace qReal {

/// Interprets reaction python script in different python interpreter process, parses output
/// and sends it to the main system
class PythonInterpreter : public QObject
{
	Q_OBJECT

public:
	explicit PythonInterpreter(QObject *parent
			, QString const pythonPath = "python"
			, QString const scriptPath = QDir().currentPath() + "/reaction.py");
	~PythonInterpreter();

	/// Interpret reaction python script
	void interpret();

	void setPythonPath(QString const &path);
	void setScriptPath(QString const &path);

signals:
	/// Emitted after parsing std output and has all properties changes
	void readyReadStdOutput(QHash<QPair<QString, QString>, QString> const &output);

	/// Raw error output from python interpreter
	void readyReadErrOutput(QString const &output);

private slots:
	/// Read python interpreter std output
	void readOutput();

	/// Read python interpreter error output
	void readErrOutput();

private:
	/// Parses interpreter std output and returns new values for element properties
	QHash<QPair<QString, QString>, QString> &parseOutput(QString const &output) const;

	/// Moves along the output and accumulates properties changes
	void parseOutput(QHash<QPair<QString, QString>, QString> &res, QString const &output, int &pos) const;

	QThread *mThread;
	QProcess *mInterpreterProcess;

	QString mPythonPath;
	QString mScriptPath;
};

}