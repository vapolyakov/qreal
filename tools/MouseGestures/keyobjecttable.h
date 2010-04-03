#pragma once
#include "GeometricForms.h"
#include "IKeyManager.h"
#include "mousemovementmanager.h"

// ��� ������� ���� ����� �������������

class KeyObjectTable
{
public:
    KeyObjectTable();
    KeyObjectTable(IKeyManager * keyManager);
    void setKeyManager(IKeyManager * keyManager);
    void add(QString const & object, QList<QPoint> const & correctPath);
    void setPath(QString const & object, QList<QPoint> const & correctPath);
    QString getObject(QList<QPoint> const & path);
    QString getKey(QString const & name);
    QList<QPoint> getPath(QString const & name);

private:
    KeyObjectItem getItem(QString const & name);
    IKeyManager * mKeyManager;
    KeyObjectItems mKeyObjectTable;
    MouseMovementManager mMouseMovementManager;
};