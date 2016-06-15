/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qqmlpropertycachecreator_p.h"

#include <private/qqmlengine_p.h>

QT_BEGIN_NAMESPACE

QAtomicInt QQmlPropertyCacheCreatorBase::classIndexCounter(0);

QQmlBindingInstantiationContext::QQmlBindingInstantiationContext()
    : referencingObjectIndex(-1)
    , instantiatingBinding(nullptr)
    , instantiatingProperty(nullptr)
{

}

QQmlBindingInstantiationContext::QQmlBindingInstantiationContext(int referencingObjectIndex, const QV4::CompiledData::Binding *instantiatingBinding, const QString &instantiatingPropertyName, const QQmlPropertyCache *referencingObjectPropertyCache)
    : referencingObjectIndex(referencingObjectIndex)
    , instantiatingBinding(instantiatingBinding)
    , instantiatingProperty(nullptr)
{
    if (instantiatingBinding && instantiatingBinding->type == QV4::CompiledData::Binding::Type_GroupProperty) {
        Q_ASSERT(referencingObjectIndex >= 0);
        Q_ASSERT(referencingObjectPropertyCache);
        Q_ASSERT(instantiatingBinding->propertyNameIndex != 0);

        bool notInRevision = false;
        instantiatingProperty = QmlIR::PropertyResolver(referencingObjectPropertyCache).property(instantiatingPropertyName, &notInRevision);
    }
}

QQmlPropertyCacheCreator::QQmlPropertyCacheCreator(QQmlPropertyCacheVector *propertyCaches, QQmlEnginePrivate *enginePrivate, const QQmlTypeCompiler *compiler, const QQmlImports *imports)
    : enginePrivate(enginePrivate)
    , compiler(compiler)
    , qmlObjects(*compiler->qmlObjects())
    , imports(imports)
    , propertyCaches(propertyCaches)
{
    propertyCaches->resize(qmlObjects.count());
}

QQmlPropertyCacheCreator::~QQmlPropertyCacheCreator()
{
}

QQmlCompileError QQmlPropertyCacheCreator::buildMetaObjects()
{
    QQmlBindingInstantiationContext context;
    return buildMetaObjectRecursively(compiler->rootObjectIndex(), context);
}

QQmlCompileError QQmlPropertyCacheCreator::buildMetaObjectRecursively(int objectIndex, const QQmlBindingInstantiationContext &context)
{
    const QmlIR::Object *obj = qmlObjects.at(objectIndex);

    bool needVMEMetaObject = obj->propertyCount() != 0 || obj->aliasCount() != 0 || obj->signalCount() != 0 || obj->functionCount() != 0;
    if (!needVMEMetaObject) {
        for (auto binding = obj->bindingsBegin(), end = obj->bindingsEnd(); binding != end; ++binding) {
            if (binding->type == QV4::CompiledData::Binding::Type_Object && (binding->flags & QV4::CompiledData::Binding::IsOnAssignment)) {
                // If the on assignment is inside a group property, we need to distinguish between QObject based
                // group properties and value type group properties. For the former the base type is derived from
                // the property that references us, for the latter we only need a meta-object on the referencing object
                // because interceptors can't go to the shared value type instances.
                if (context.instantiatingProperty && QQmlValueTypeFactory::isValueType(context.instantiatingProperty->propType)) {
                    if (!propertyCaches->needsVMEMetaObject(context.referencingObjectIndex)) {
                        const QmlIR::Object *obj = qmlObjects.at(context.referencingObjectIndex);
                        auto *typeRef = compiler->resolvedTypes.value(obj->inheritedTypeNameIndex);
                        Q_ASSERT(typeRef);
                        QQmlPropertyCache *baseTypeCache = typeRef->createPropertyCache(QQmlEnginePrivate::get(enginePrivate));
                        QQmlCompileError error = createMetaObject(context.referencingObjectIndex, obj, baseTypeCache);
                        if (error.isSet())
                            return error;
                    }
                } else {
                    // On assignments are implemented using value interceptors, which require a VME meta object.
                    needVMEMetaObject = true;
                }
                break;
            }
        }
    }

    QQmlPropertyCache *baseTypeCache;
    {
        QQmlCompileError error;
        baseTypeCache = propertyCacheForObject(obj, context, &error);
        if (error.isSet())
            return error;
    }

    if (baseTypeCache) {
        if (needVMEMetaObject) {
            QQmlCompileError error = createMetaObject(objectIndex, obj, baseTypeCache);
            if (error.isSet())
                return error;
        } else {
            propertyCaches->set(objectIndex, baseTypeCache);
        }
    }

    if (QQmlPropertyCache *thisCache = propertyCaches->at(objectIndex)) {
        for (auto binding = obj->bindingsBegin(), end = obj->bindingsEnd(); binding != end; ++binding)
            if (binding->type >= QV4::CompiledData::Binding::Type_Object) {
                QQmlBindingInstantiationContext context(objectIndex, &(*binding), stringAt(binding->propertyNameIndex), thisCache);
                QQmlCompileError error = buildMetaObjectRecursively(binding->value.objectIndex, context);
                if (error.isSet())
                    return error;
            }
    }

    QQmlCompileError noError;
    return noError;
}

QQmlPropertyCache *QQmlPropertyCacheCreator::propertyCacheForObject(const QmlIR::Object *obj, const QQmlBindingInstantiationContext &context, QQmlCompileError *error) const
{
    if (context.instantiatingProperty) {
        if (context.instantiatingProperty->isQObject()) {
            return enginePrivate->rawPropertyCacheForType(context.instantiatingProperty->propType);
        } else if (const QMetaObject *vtmo = QQmlValueTypeFactory::metaObjectForMetaType(context.instantiatingProperty->propType)) {
            return enginePrivate->cache(vtmo);
        }
    } else if (obj->inheritedTypeNameIndex != 0) {
        auto *typeRef = compiler->resolvedTypes.value(obj->inheritedTypeNameIndex);
        Q_ASSERT(typeRef);

        if (typeRef->isFullyDynamicType) {
            if (obj->propertyCount() > 0 || obj->aliasCount() > 0) {
                *error = QQmlCompileError(obj->location, QQmlPropertyCacheCreatorBase::tr("Fully dynamic types cannot declare new properties."));
                return nullptr;
            }
            if (obj->signalCount() > 0) {
                *error = QQmlCompileError(obj->location, QQmlPropertyCacheCreatorBase::tr("Fully dynamic types cannot declare new signals."));
                return nullptr;
            }
            if (obj->functionCount() > 0) {
                *error = QQmlCompileError(obj->location, QQmlPropertyCacheCreatorBase::tr("Fully Dynamic types cannot declare new functions."));
                return nullptr;
            }
        }

        return typeRef->createPropertyCache(QQmlEnginePrivate::get(enginePrivate));
    } else if (context.instantiatingBinding && context.instantiatingBinding->isAttachedProperty()) {
        auto *typeRef = compiler->resolvedTypes.value(context.instantiatingBinding->propertyNameIndex);
        Q_ASSERT(typeRef);
        QQmlType *qmltype = typeRef->type;
        if (!qmltype) {
            QString propertyName = stringAt(context.instantiatingBinding->propertyNameIndex);
            if (imports->resolveType(propertyName, &qmltype, 0, 0, 0)) {
                if (qmltype->isComposite()) {
                    QQmlTypeData *tdata = enginePrivate->typeLoader.getType(qmltype->sourceUrl());
                    Q_ASSERT(tdata);
                    Q_ASSERT(tdata->isComplete());

                    auto compilationUnit = tdata->compilationUnit();
                    qmltype = QQmlMetaType::qmlType(compilationUnit->metaTypeId);

                    tdata->release();
                }
            }
        }

        const QMetaObject *attachedMo = qmltype ? qmltype->attachedPropertiesType(enginePrivate) : 0;
        if (!attachedMo) {
            *error = QQmlCompileError(context.instantiatingBinding->location, QQmlPropertyCacheCreatorBase::tr("Non-existent attached object"));
            return nullptr;
        }
        return enginePrivate->cache(attachedMo);
    }
    return nullptr;
}

QQmlCompileError QQmlPropertyCacheCreator::createMetaObject(int objectIndex, const QmlIR::Object *obj, QQmlPropertyCache *baseTypeCache)
{
    QQmlRefPointer<QQmlPropertyCache> cache;
    cache.adopt(baseTypeCache->copyAndReserve(obj->propertyCount() + obj->aliasCount(),
                                              obj->functionCount() + obj->propertyCount() + obj->aliasCount() + obj->signalCount(),
                                              obj->signalCount() + obj->propertyCount() + obj->aliasCount()));

    propertyCaches->set(objectIndex, cache);
    propertyCaches->setNeedsVMEMetaObject(objectIndex);

    struct TypeData {
        QV4::CompiledData::Property::Type dtype;
        int metaType;
    } builtinTypes[] = {
        { QV4::CompiledData::Property::Var, QMetaType::QVariant },
        { QV4::CompiledData::Property::Variant, QMetaType::QVariant },
        { QV4::CompiledData::Property::Int, QMetaType::Int },
        { QV4::CompiledData::Property::Bool, QMetaType::Bool },
        { QV4::CompiledData::Property::Real, QMetaType::Double },
        { QV4::CompiledData::Property::String, QMetaType::QString },
        { QV4::CompiledData::Property::Url, QMetaType::QUrl },
        { QV4::CompiledData::Property::Color, QMetaType::QColor },
        { QV4::CompiledData::Property::Font, QMetaType::QFont },
        { QV4::CompiledData::Property::Time, QMetaType::QTime },
        { QV4::CompiledData::Property::Date, QMetaType::QDate },
        { QV4::CompiledData::Property::DateTime, QMetaType::QDateTime },
        { QV4::CompiledData::Property::Rect, QMetaType::QRectF },
        { QV4::CompiledData::Property::Point, QMetaType::QPointF },
        { QV4::CompiledData::Property::Size, QMetaType::QSizeF },
        { QV4::CompiledData::Property::Vector2D, QMetaType::QVector2D },
        { QV4::CompiledData::Property::Vector3D, QMetaType::QVector3D },
        { QV4::CompiledData::Property::Vector4D, QMetaType::QVector4D },
        { QV4::CompiledData::Property::Matrix4x4, QMetaType::QMatrix4x4 },
        { QV4::CompiledData::Property::Quaternion, QMetaType::QQuaternion }
    };
    static const uint builtinTypeCount = sizeof(builtinTypes) / sizeof(TypeData);

    QByteArray newClassName;

    if (objectIndex == compiler->rootObjectIndex()) {
        const QString path = compiler->url().path();
        int lastSlash = path.lastIndexOf(QLatin1Char('/'));
        if (lastSlash > -1) {
            const QStringRef nameBase = path.midRef(lastSlash + 1, path.length() - lastSlash - 5);
            if (!nameBase.isEmpty() && nameBase.at(0).isUpper())
                newClassName = nameBase.toUtf8() + "_QMLTYPE_" +
                               QByteArray::number(classIndexCounter.fetchAndAddRelaxed(1));
        }
    }
    if (newClassName.isEmpty()) {
        newClassName = QQmlMetaObject(baseTypeCache).className();
        newClassName.append("_QML_");
        newClassName.append(QByteArray::number(classIndexCounter.fetchAndAddRelaxed(1)));
    }

    cache->_dynamicClassName = newClassName;

    int varPropCount = 0;

    QmlIR::PropertyResolver resolver(baseTypeCache);

    for (auto p = obj->propertiesBegin(), end = obj->propertiesEnd(); p != end; ++p) {
        if (p->type == QV4::CompiledData::Property::Var)
            varPropCount++;

        bool notInRevision = false;
        QQmlPropertyData *d = resolver.property(stringAt(p->nameIndex), &notInRevision);
        if (d && d->isFinal())
            return QQmlCompileError(p->location, QQmlPropertyCacheCreatorBase::tr("Cannot override FINAL property"));
    }

    for (auto a = obj->aliasesBegin(), end = obj->aliasesEnd(); a != end; ++a) {
        bool notInRevision = false;
        QQmlPropertyData *d = resolver.property(stringAt(a->nameIndex), &notInRevision);
        if (d && d->isFinal())
            return QQmlCompileError(a->location, QQmlPropertyCacheCreatorBase::tr("Cannot override FINAL property"));
    }

    int effectivePropertyIndex = cache->propertyIndexCacheStart;
    int effectiveMethodIndex = cache->methodIndexCacheStart;

    // For property change signal override detection.
    // We prepopulate a set of signal names which already exist in the object,
    // and throw an error if there is a signal/method defined as an override.
    QSet<QString> seenSignals;
    seenSignals << QStringLiteral("destroyed") << QStringLiteral("parentChanged") << QStringLiteral("objectNameChanged");
    QQmlPropertyCache *parentCache = cache;
    while ((parentCache = parentCache->parent())) {
        if (int pSigCount = parentCache->signalCount()) {
            int pSigOffset = parentCache->signalOffset();
            for (int i = pSigOffset; i < pSigCount; ++i) {
                QQmlPropertyData *currPSig = parentCache->signal(i);
                // XXX TODO: find a better way to get signal name from the property data :-/
                for (QQmlPropertyCache::StringCache::ConstIterator iter = parentCache->stringCache.begin();
                        iter != parentCache->stringCache.end(); ++iter) {
                    if (currPSig == (*iter).second) {
                        seenSignals.insert(iter.key());
                        break;
                    }
                }
            }
        }
    }

    // Set up notify signals for properties - first normal, then alias
    for (auto p = obj->propertiesBegin(), end = obj->propertiesEnd(); p != end; ++p) {
        quint32 flags = QQmlPropertyData::IsSignal | QQmlPropertyData::IsFunction |
                QQmlPropertyData::IsVMESignal;

        QString changedSigName = stringAt(p->nameIndex) + QLatin1String("Changed");
        seenSignals.insert(changedSigName);

        cache->appendSignal(changedSigName, flags, effectiveMethodIndex++);
    }

    for (auto a = obj->aliasesBegin(), end = obj->aliasesEnd(); a != end; ++a) {
        quint32 flags = QQmlPropertyData::IsSignal | QQmlPropertyData::IsFunction |
                        QQmlPropertyData::IsVMESignal;

        QString changedSigName = stringAt(a->nameIndex) + QLatin1String("Changed");
        seenSignals.insert(changedSigName);

        cache->appendSignal(changedSigName, flags, effectiveMethodIndex++);
    }

    // Dynamic signals
    for (auto s = obj->signalsBegin(), end = obj->signalsEnd(); s != end; ++s) {
        const int paramCount = s->parameterCount();

        QList<QByteArray> names;
        names.reserve(paramCount);
        QVarLengthArray<int, 10> paramTypes(paramCount?(paramCount + 1):0);

        if (paramCount) {
            paramTypes[0] = paramCount;

            int i = 0;
            for (auto param = s->parametersBegin(), end = s->parametersEnd(); param != end; ++param, ++i) {
                names.append(stringAt(param->nameIndex).toUtf8());
                if (param->type < builtinTypeCount) {
                    // built-in type
                    paramTypes[i + 1] = builtinTypes[param->type].metaType;
                } else {
                    // lazily resolved type
                    Q_ASSERT(param->type == QV4::CompiledData::Property::Custom);
                    const QString customTypeName = stringAt(param->customTypeNameIndex);
                    QQmlType *qmltype = 0;
                    if (!imports->resolveType(customTypeName, &qmltype, 0, 0, 0))
                        return QQmlCompileError(s->location, QQmlPropertyCacheCreatorBase::tr("Invalid signal parameter type: %1").arg(customTypeName));

                    if (qmltype->isComposite()) {
                        QQmlTypeData *tdata = enginePrivate->typeLoader.getType(qmltype->sourceUrl());
                        Q_ASSERT(tdata);
                        Q_ASSERT(tdata->isComplete());

                        auto compilationUnit = tdata->compilationUnit();

                        paramTypes[i + 1] = compilationUnit->metaTypeId;

                        tdata->release();
                    } else {
                        paramTypes[i + 1] = qmltype->typeId();
                    }
                }
            }
        }

        quint32 flags = QQmlPropertyData::IsSignal | QQmlPropertyData::IsFunction |
                        QQmlPropertyData::IsVMESignal;
        if (paramCount)
            flags |= QQmlPropertyData::HasArguments;

        QString signalName = stringAt(s->nameIndex);
        if (seenSignals.contains(signalName))
            return QQmlCompileError(s->location, QQmlPropertyCacheCreatorBase::tr("Duplicate signal name: invalid override of property change signal or superclass signal"));
        seenSignals.insert(signalName);

        cache->appendSignal(signalName, flags, effectiveMethodIndex++,
                            paramCount?paramTypes.constData():0, names);
    }


    // Dynamic slots
    for (auto function = compiler->objectFunctionsBegin(obj), end = compiler->objectFunctionsEnd(obj); function != end; ++function) {
        quint32 flags = QQmlPropertyData::IsFunction | QQmlPropertyData::IsVMEFunction;

        const QString slotName = stringAt(function->nameIndex);
        if (seenSignals.contains(slotName))
            return QQmlCompileError(function->location, QQmlPropertyCacheCreatorBase::tr("Duplicate method name: invalid override of property change signal or superclass signal"));
        // Note: we don't append slotName to the seenSignals list, since we don't
        // protect against overriding change signals or methods with properties.

        QList<QByteArray> parameterNames;
        for (auto formal = function->formalsBegin(), end = function->formalsEnd(); formal != end; ++formal) {
            flags |= QQmlPropertyData::HasArguments;
            parameterNames << stringAt(*formal).toUtf8();
        }

        cache->appendMethod(slotName, flags, effectiveMethodIndex++, parameterNames);
    }


    // Dynamic properties
    int effectiveSignalIndex = cache->signalHandlerIndexCacheStart;
    int propertyIdx = 0;
    for (auto p = obj->propertiesBegin(), end = obj->propertiesEnd(); p != end; ++p, ++propertyIdx) {
        int propertyType = 0;
        quint32 propertyFlags = 0;

        if (p->type == QV4::CompiledData::Property::Var) {
            propertyType = QMetaType::QVariant;
            propertyFlags = QQmlPropertyData::IsVarProperty;
        } else if (p->type < builtinTypeCount) {
            propertyType = builtinTypes[p->type].metaType;

            if (p->type == QV4::CompiledData::Property::Variant)
                propertyFlags |= QQmlPropertyData::IsQVariant;
        } else {
            Q_ASSERT(p->type == QV4::CompiledData::Property::CustomList ||
                     p->type == QV4::CompiledData::Property::Custom);

            QQmlType *qmltype = 0;
            if (!imports->resolveType(stringAt(p->customTypeNameIndex), &qmltype, 0, 0, 0)) {
                return QQmlCompileError(p->location, QQmlPropertyCacheCreatorBase::tr("Invalid property type"));
            }

            Q_ASSERT(qmltype);
            if (qmltype->isComposite()) {
                QQmlTypeData *tdata = enginePrivate->typeLoader.getType(qmltype->sourceUrl());
                Q_ASSERT(tdata);
                Q_ASSERT(tdata->isComplete());

                auto compilationUnit = tdata->compilationUnit();

                if (p->type == QV4::CompiledData::Property::Custom) {
                    propertyType = compilationUnit->metaTypeId;
                } else {
                    propertyType = compilationUnit->listMetaTypeId;
                }

                tdata->release();
            } else {
                if (p->type == QV4::CompiledData::Property::Custom) {
                    propertyType = qmltype->typeId();
                } else {
                    propertyType = qmltype->qListTypeId();
                }
            }

            if (p->type == QV4::CompiledData::Property::Custom)
                propertyFlags |= QQmlPropertyData::IsQObjectDerived;
            else
                propertyFlags |= QQmlPropertyData::IsQList;
        }

        if (!(p->flags & QV4::CompiledData::Property::IsReadOnly) && p->type != QV4::CompiledData::Property::CustomList)
            propertyFlags |= QQmlPropertyData::IsWritable;


        QString propertyName = stringAt(p->nameIndex);
        if (!obj->defaultPropertyIsAlias && propertyIdx == obj->indexOfDefaultPropertyOrAlias)
            cache->_defaultPropertyName = propertyName;
        cache->appendProperty(propertyName, propertyFlags, effectivePropertyIndex++,
                              propertyType, effectiveSignalIndex);

        effectiveSignalIndex++;
    }

    QQmlCompileError noError;
    return noError;
}

QT_END_NAMESPACE
