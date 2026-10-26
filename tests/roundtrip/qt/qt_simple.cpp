// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "model.h"

Model::Model(QObject* parent) : QObject(parent) {}
QString Model::title() const { return QString(); }
void Model::setTitle(const QString&) {}
