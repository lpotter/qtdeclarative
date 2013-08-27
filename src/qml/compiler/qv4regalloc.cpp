/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the V4VM module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qv4regalloc_p.h"

//#define DEBUG_REGALLOC

namespace {
struct Use {
    enum RegisterFlag { MustHaveRegister = 0, CouldHaveRegister = 1 };
    unsigned flag : 1;
    unsigned pos  : 31;

    Use(): pos(0), flag(MustHaveRegister) {}
    Use(int pos, RegisterFlag flag): flag(flag), pos(pos) {}

    bool mustHaveRegister() const { return flag == MustHaveRegister; }
};
}

QT_BEGIN_NAMESPACE
using namespace QQmlJS::V4IR;

namespace QQmlJS {
namespace MASM {

class RegAllocInfo: public IRDecoder
{
    struct Def {
        unsigned defStmt : 30;
        unsigned canHaveReg : 1;
        unsigned isPhiTarget : 1;

        Def(): defStmt(0), canHaveReg(0), isPhiTarget(0) {}
        Def(int defStmt, bool canHaveReg, bool isPhiTarget)
            : defStmt(defStmt), canHaveReg(canHaveReg), isPhiTarget(isPhiTarget)
        {
            Q_ASSERT(defStmt > 0);
            Q_ASSERT(defStmt < (1 << 30));
        }

        bool isValid() const { return defStmt != 0; } // 0 is invalid, as stmt numbers start at 1.
    };

    Stmt *_currentStmt;
    QHash<Temp, Def> _defs;
    QHash<Temp, QList<Use> > _uses;
    QList<int> _calls;
    QHash<Temp, QList<Temp> > _hints;

public:
    RegAllocInfo(): _currentStmt(0) {}

    void collect(Function *function)
    {
        foreach (BasicBlock *bb, function->basicBlocks) {
            foreach (Stmt *s, bb->statements) {
                Q_ASSERT(s->id > 0);
                _currentStmt = s;
                s->accept(this);
            }
        }
    }

    QList<Use> uses(const Temp &t) const { return _uses[t]; }
    int def(const Temp &t) const {
        Q_ASSERT(_defs[t].isValid());
        return _defs[t].defStmt;
    }
    bool canHaveRegister(const Temp &t) const {
        Q_ASSERT(_defs[t].isValid());
        return _defs[t].canHaveReg;
    }
    bool isPhiTarget(const Temp &t) const {
        Q_ASSERT(_defs[t].isValid());
        return _defs[t].isPhiTarget;
    }

    QList<int> calls() const { return _calls; }
    QList<Temp> hints(const Temp &t) const { return _hints[t]; }
    void addHint(const Temp &t, int physicalRegister)
    {
        Temp hint;
        hint.init(Temp::PhysicalRegister, physicalRegister, 0);
        _hints[t].append(hint);
    }

#ifdef DEBUG_REGALLOC
    void dump() const
    {
        QTextStream qout(stdout, QIODevice::WriteOnly);

        qout << "RegAllocInfo:" << endl << "Defs/uses:" << endl;
        QList<Temp> temps = _defs.keys();
        qSort(temps);
        foreach (const Temp &t, temps) {
            t.dump(qout);
            qout << " def at " << _defs[t].defStmt << " ("
                 << (_defs[t].canHaveReg ? "can" : "can NOT")
                 << " have a register, and "
                 << (isPhiTarget(t) ? "is" : "is NOT")
                 << " defined by a phi node), uses at: ";
            const QList<Use> &uses = _uses[t];
            for (int i = 0; i < uses.size(); ++i) {
                if (i > 0) qout << ", ";
                qout << uses[i].pos;
                if (uses[i].mustHaveRegister()) qout << "(R)"; else qout << "(S)";
            }
            qout << endl;
        }

        qout << "Calls at: ";
        for (int i = 0; i < _calls.size(); ++i) {
            if (i > 0) qout << ", ";
            qout << _calls[i];
        }
        qout << endl;

        qout << "Hints:" << endl;
        QList<Temp> hinted = _hints.keys();
        if (hinted.isEmpty())
            qout << "\t(none)" << endl;
        qSort(hinted);
        foreach (const Temp &t, hinted) {
            qout << "\t";
            t.dump(qout);
            qout << ": ";
            QList<Temp> hints = _hints[t];
            for (int i = 0; i < hints.size(); ++i) {
                if (i > 0) qout << ", ";
                hints[i].dump(qout);
            }
            qout << endl;
        }
    }
#endif // DEBUG_REGALLOC

protected: // IRDecoder
    virtual void callBuiltinInvalid(V4IR::Name *, V4IR::ExprList *, V4IR::Temp *) {}
    virtual void callBuiltinTypeofMember(V4IR::Expr *, const QString &, V4IR::Temp *) {}
    virtual void callBuiltinTypeofSubscript(V4IR::Expr *, V4IR::Expr *, V4IR::Temp *) {}
    virtual void callBuiltinTypeofName(const QString &, V4IR::Temp *) {}
    virtual void callBuiltinTypeofValue(V4IR::Expr *, V4IR::Temp *) {}
    virtual void callBuiltinDeleteMember(V4IR::Temp *, const QString &, V4IR::Temp *) {}
    virtual void callBuiltinDeleteSubscript(V4IR::Temp *, V4IR::Expr *, V4IR::Temp *) {}
    virtual void callBuiltinDeleteName(const QString &, V4IR::Temp *) {}
    virtual void callBuiltinDeleteValue(V4IR::Temp *) {}
    virtual void callBuiltinPostDecrementMember(V4IR::Temp *, const QString &, V4IR::Temp *) {}
    virtual void callBuiltinPostDecrementSubscript(V4IR::Temp *, V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinPostDecrementName(const QString &, V4IR::Temp *) {}
    virtual void callBuiltinPostDecrementValue(V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinPostIncrementMember(V4IR::Temp *, const QString &, V4IR::Temp *) {}
    virtual void callBuiltinPostIncrementSubscript(V4IR::Temp *, V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinPostIncrementName(const QString &, V4IR::Temp *) {}
    virtual void callBuiltinPostIncrementValue(V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinThrow(V4IR::Expr *) {}
    virtual void callBuiltinFinishTry() {}
    virtual void callBuiltinForeachIteratorObject(V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinForeachNextProperty(V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinForeachNextPropertyname(V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinPushWithScope(V4IR::Temp *) {}
    virtual void callBuiltinPopScope() {}
    virtual void callBuiltinDeclareVar(bool , const QString &) {}
    virtual void callBuiltinDefineGetterSetter(V4IR::Temp *, const QString &, V4IR::Temp *, V4IR::Temp *) {}
    virtual void callBuiltinDefineProperty(V4IR::Temp *, const QString &, V4IR::Expr *) {}
    virtual void callBuiltinDefineArray(V4IR::Temp *, V4IR::ExprList *) {}
    virtual void callBuiltinDefineObjectLiteral(V4IR::Temp *, V4IR::ExprList *) {}
    virtual void callBuiltinSetupArgumentObject(V4IR::Temp *) {}

    virtual void callValue(V4IR::Temp *value, V4IR::ExprList *args, V4IR::Temp *result)
    {
        addDef(result);
        addUses(value, Use::CouldHaveRegister);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void callProperty(V4IR::Expr *base, const QString &name, V4IR::ExprList *args,
                              V4IR::Temp *result)
    {
        addDef(result);
        addUses(base->asTemp(), Use::CouldHaveRegister);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void callSubscript(V4IR::Expr *base, V4IR::Expr *index, V4IR::ExprList *args,
                               V4IR::Temp *result)
    {
        addDef(result);
        addUses(base->asTemp(), Use::CouldHaveRegister);
        addUses(index->asTemp(), Use::CouldHaveRegister);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void convertType(V4IR::Temp *source, V4IR::Temp *target)
    {
        // TODO: do not generate a call (meaning: block all registers), but annotate the conversion with registers that need to be saved and have masm take care of that.
        addDef(target);

        bool needsCall = true;
        Use::RegisterFlag sourceReg = Use::CouldHaveRegister;

        // TODO: verify this method
        switch (target->type) {
        case DoubleType:
            if (source->type == UInt32Type) {
                sourceReg = Use::MustHaveRegister;
                needsCall = false;
                break;
            }
#if 0 // TODO: change masm to generate code
        case SInt32Type:
        case UInt32Type:
        case BoolType:
#endif
            switch (source->type) {
            case BoolType:
            case DoubleType:
                sourceReg = Use::MustHaveRegister;
                needsCall = false;
                break;
            case SInt32Type:
                needsCall = false;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }

        addUses(source, sourceReg);

        if (needsCall)
            addCall();
        else
            addHint(target, source);
    }

    virtual void constructActivationProperty(V4IR::Name *, V4IR::ExprList *args, V4IR::Temp *result)
    {
        addDef(result);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void constructProperty(V4IR::Temp *base, const QString &, V4IR::ExprList *args, V4IR::Temp *result)
    {
        addDef(result);
        addUses(base, Use::CouldHaveRegister);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void constructValue(V4IR::Temp *value, V4IR::ExprList *args, V4IR::Temp *result)
    {
        addDef(result);
        addUses(value, Use::CouldHaveRegister);
        addUses(args, Use::CouldHaveRegister);
        addCall();
    }

    virtual void loadThisObject(V4IR::Temp *temp)
    {
        addDef(temp);
        addCall(); // FIXME: propagate this
    }

    virtual void loadConst(V4IR::Const *sourceConst, V4IR::Temp *targetTemp)
    {
        addDef(targetTemp);
    }

    virtual void loadString(const QString &str, V4IR::Temp *targetTemp)
    {
        addDef(targetTemp);
    }

    virtual void loadRegexp(V4IR::RegExp *sourceRegexp, V4IR::Temp *targetTemp)
    {
        addDef(targetTemp);
        addCall();
    }

    virtual void getActivationProperty(const V4IR::Name *, V4IR::Temp *temp)
    {
        addDef(temp);
        addCall();
    }

    virtual void setActivationProperty(V4IR::Expr *source, const QString &)
    {
        addUses(source->asTemp(), Use::CouldHaveRegister);
        addCall();
    }

    virtual void initClosure(V4IR::Closure *closure, V4IR::Temp *target)
    {
        addDef(target);
        addCall();
    }

    virtual void getProperty(V4IR::Expr *base, const QString &, V4IR::Temp *target)
    {
        addDef(target);
        addUses(base->asTemp(), Use::CouldHaveRegister);
        addCall();
    }

    virtual void setProperty(V4IR::Expr *source, V4IR::Expr *targetBase, const QString &)
    {
        addUses(source->asTemp(), Use::CouldHaveRegister);
        addUses(targetBase->asTemp(), Use::CouldHaveRegister);
        addCall();
    }

    virtual void getElement(V4IR::Expr *base, V4IR::Expr *index, V4IR::Temp *target)
    {
        addDef(target);
        addUses(base->asTemp(), Use::CouldHaveRegister);
        addUses(index->asTemp(), Use::CouldHaveRegister);
        addCall();
    }

    virtual void setElement(V4IR::Expr *source, V4IR::Expr *targetBase, V4IR::Expr *targetIndex)
    {
        addUses(source->asTemp(), Use::CouldHaveRegister);
        addUses(targetBase->asTemp(), Use::CouldHaveRegister);
        addUses(targetIndex->asTemp(), Use::CouldHaveRegister);
        addCall();
    }

    virtual void copyValue(V4IR::Temp *sourceTemp, V4IR::Temp *targetTemp)
    {
        addDef(targetTemp);
        addUses(sourceTemp, Use::CouldHaveRegister);
        addHint(targetTemp, sourceTemp);
    }

    virtual void swapValues(V4IR::Temp *, V4IR::Temp *)
    {
        // Inserted by the register allocator, so it cannot occur here.
        Q_UNREACHABLE();
    }

    virtual void unop(AluOp oper, Temp *sourceTemp, Temp *targetTemp)
    {
        addDef(targetTemp);

        bool needsCall = true;
#if 0 // TODO: change masm to generate code
        switch (oper) {
        case OpIfTrue:
        case OpNot:
        case OpUMinus:
        case OpUPlus:
        case OpCompl:
            needsCall = sourceTemp->type & ~NumberType && sourceTemp->type != BoolType;
            break;

        case OpIncrement:
        case OpDecrement:
        default:
            Q_UNREACHABLE();
        }
#endif

        if (needsCall) {
            addUses(sourceTemp, Use::CouldHaveRegister);
            addCall();
        } else {
            addUses(sourceTemp, Use::MustHaveRegister);
        }
    }

    virtual void binop(AluOp oper, Expr *leftSource, Expr *rightSource, Temp *target)
    {
        bool needsCall = true;

#if 0 // TODO: change masm to generate code
        switch (leftSource->type) {
        case DoubleType:
        case SInt32Type:
        case UInt32Type:
            switch (rightSource->type) {
            case DoubleType:
            case SInt32Type:
            case UInt32Type:
                if (oper != OpMod)
                    needsCall = false;
            default:
                break;
            } break;
        default:
            break;
        }
#endif

        addDef(target);

        if (needsCall) {
            addUses(leftSource->asTemp(), Use::CouldHaveRegister);
            addUses(rightSource->asTemp(), Use::CouldHaveRegister);
            addCall();
        } else {
            addUses(leftSource->asTemp(), Use::MustHaveRegister);
            addHint(target, leftSource->asTemp());

            addUses(rightSource->asTemp(), Use::MustHaveRegister);
            switch (oper) {
            case OpAdd:
            case OpMul:
                addHint(target, rightSource->asTemp());
                break;
            default:
                break;
            }
        }
    }

    virtual void inplaceNameOp(V4IR::AluOp oper, V4IR::Temp *rightSource, const QString &targetName)
    {
        Q_UNREACHABLE();
        // TODO: remove this!
    }

    virtual void inplaceElementOp(V4IR::AluOp oper, V4IR::Temp *source, V4IR::Temp *targetBaseTemp, V4IR::Temp *targetIndexTemp)
    {
        Q_UNREACHABLE();
        // TODO: remove this!
    }

    virtual void inplaceMemberOp(V4IR::AluOp oper, V4IR::Temp *source, V4IR::Temp *targetBase, const QString &targetName)
    {
        Q_UNREACHABLE();
        // TODO: remove this!
    }

    virtual void visitJump(V4IR::Jump *) {}
    virtual void visitCJump(V4IR::CJump *s)
    {
        if (Temp *t = s->cond->asTemp()) {
#if 0 // TODO: change masm to generate code
            addUses(t, Use::MustHaveRegister);
#else
            addUses(t, Use::CouldHaveRegister);
            addCall();
#endif
        } else if (Binop *b = s->cond->asBinop()) {
            binop(b->op, b->left, b->right, 0);
        } else if (Const *c = s->cond->asConst()) {
            // TODO: SSA optimization for constant condition evaluation should remove this.
            // See also visitCJump() in masm.
            addCall();
        } else {
            Q_UNREACHABLE();
        }
    }

    virtual void visitRet(V4IR::Ret *s)
    { addUses(s->expr->asTemp(), Use::CouldHaveRegister); }

    virtual void visitTry(V4IR::Try *)
    { Q_UNREACHABLE(); } // this should never happen, we do not optimize when there is a try in the function

    virtual void visitPhi(V4IR::Phi *s)
    {
        addDef(s->targetTemp, true);
        foreach (Expr *e, s->d->incoming) {
            if (Temp *t = e->asTemp()) {
                addUses(t, Use::CouldHaveRegister);
                addHint(s->targetTemp, t);
            }
        }
    }

protected:
    virtual void callBuiltin(V4IR::Call *c, V4IR::Temp *result)
    {
        addDef(result);
        addUses(c->base->asTemp(), Use::CouldHaveRegister);
        addUses(c->args, Use::CouldHaveRegister);
        addCall();
    }

private:
    void addDef(Temp *t, bool isPhiTarget = false)
    {
        if (!t || t->kind != Temp::VirtualRegister)
            return;
        Q_ASSERT(!_defs.contains(*t));
        bool canHaveReg = true;
        switch (t->type) {
        case ObjectType:
        case StringType:
        case UndefinedType:
        case NullType:
            canHaveReg = false;
            break;
        default:
            break;
        }

        _defs[*t] = Def(_currentStmt->id, canHaveReg, isPhiTarget);
    }

    void addUses(Temp *t, Use::RegisterFlag flag)
    {
        Q_ASSERT(_currentStmt->id > 0);
        if (t && t->kind == Temp::VirtualRegister)
            _uses[*t].append(Use(_currentStmt->id, flag));
    }

    void addUses(ExprList *l, Use::RegisterFlag flag)
    {
        for (ExprList *it = l; it; it = it->next)
            addUses(it->expr->asTemp(), flag);
    }

    void addCall()
    {
        _calls.append(_currentStmt->id);
    }

    void addHint(Temp *hinted, Temp *hint1, Temp *hint2 = 0)
    {
        if (!hinted || hinted->kind != Temp::VirtualRegister)
            return;
        if (hint1 && hint1->kind == Temp::VirtualRegister && hinted->type == hint1->type)
            _hints[*hinted].append(*hint1);
        if (hint2 && hint2->kind == Temp::VirtualRegister && hinted->type == hint2->type)
            _hints[*hinted].append(*hint2);
    }
};

} // MASM namespace
} // MOTH namespace
QT_END_NAMESPACE

QT_USE_NAMESPACE

using namespace QT_PREPEND_NAMESPACE(QQmlJS::MASM);
using namespace QT_PREPEND_NAMESPACE(QQmlJS::V4IR);
using namespace QT_PREPEND_NAMESPACE(QQmlJS);

namespace {
class ResolutionPhase: protected StmtVisitor, protected ExprVisitor {
    QList<LifeTimeInterval> _intervals;
    Function *_function;
    RegAllocInfo *_info;
    const QHash<V4IR::Temp, int> &_assignedSpillSlots;
    QHash<V4IR::Temp, LifeTimeInterval> _intervalForTemp;
    const QVector<int> &_intRegs;
    const QVector<int> &_fpRegs;

    Stmt *_currentStmt;
    QVector<Move *> _loads;
    QVector<Move *> _stores;

    QHash<BasicBlock *, QList<LifeTimeInterval> > _liveAtStart;
    QHash<BasicBlock *, QList<LifeTimeInterval> > _liveAtEnd;

public:
    ResolutionPhase(const QList<LifeTimeInterval> &intervals, Function *function, RegAllocInfo *info,
                    const QHash<V4IR::Temp, int> &assignedSpillSlots,
                    const QVector<int> &intRegs, const QVector<int> &fpRegs)
        : _intervals(intervals)
        , _function(function)
        , _info(info)
        , _assignedSpillSlots(assignedSpillSlots)
        , _intRegs(intRegs)
        , _fpRegs(fpRegs)
    {
    }

    void run() {
        renumber();
        Optimizer::showMeTheCode(_function);
        resolve();
    }

private:
    void renumber()
    {
        foreach (BasicBlock *bb, _function->basicBlocks) {
            QVector<Stmt *> newStatements;

            bool seenFirstNonPhiStmt = false;
            for (int i = 0, ei = bb->statements.size(); i != ei; ++i) {
                _currentStmt = bb->statements[i];
                _loads.clear();
                _stores.clear();
                addNewIntervals();
                if (!seenFirstNonPhiStmt && !_currentStmt->asPhi()) {
                    seenFirstNonPhiStmt = true;
                    _liveAtStart[bb] = _intervalForTemp.values();
                }
                _currentStmt->accept(this);
                foreach (Move *load, _loads)
                    newStatements.append(load);
                if (_currentStmt->asPhi())
                    newStatements.prepend(_currentStmt);
                else
                    newStatements.append(_currentStmt);
                foreach (Move *store, _stores)
                    newStatements.append(store);
            }

            cleanOldIntervals();
            _liveAtEnd[bb] = _intervalForTemp.values();

#ifdef DEBUG_REGALLOC
            QTextStream os(stdout, QIODevice::WriteOnly);
            os << "Intervals live at the start of L" << bb->index << ":" << endl;
            if (_liveAtStart[bb].isEmpty())
                os << "\t(none)" << endl;
            foreach (const LifeTimeInterval &i, _liveAtStart[bb]) {
                os << "\t";
                i.dump(os);
                os << endl;
            }
            os << "Intervals live at the end of L" << bb->index << ":" << endl;
            if (_liveAtEnd[bb].isEmpty())
                os << "\t(none)" << endl;
            foreach (const LifeTimeInterval &i, _liveAtEnd[bb]) {
                os << "\t";
                i.dump(os);
                os << endl;
            }
#endif

            bb->statements = newStatements;
        }

    }

    void activate(const LifeTimeInterval &i)
    {
        Q_ASSERT(!i.isFixedInterval());
        _intervalForTemp[i.temp()] = i;

        if (i.reg() != LifeTimeInterval::Invalid) {
            // check if we need to generate spill/unspill instructions
            if (i.start() == _currentStmt->id) {
                if (i.isSplitFromInterval()) {
                    int pReg = platformRegister(i);
                    _loads.append(generateUnspill(i.temp(), pReg));
                } else {
                    int pReg = platformRegister(i);
                    int spillSlot = _assignedSpillSlots.value(i.temp(), -1);
                    if (spillSlot != -1)
                        _stores.append(generateSpill(spillSlot, i.temp().type, pReg));
                }
            }
        }
    }

    void addNewIntervals()
    {
        if (Phi *phi = _currentStmt->asPhi()) {
            // for phi nodes, only activate the range belonging to that node
            for (int it = 0, eit = _intervals.size(); it != eit; ++it) {
                const LifeTimeInterval &i = _intervals.at(it);
                if (i.start() > _currentStmt->id)
                    break;
                if (i.temp() == *phi->targetTemp) {
                    activate(i);
                    _intervals.removeAt(it);
                    break;
                }
            }
            return;
        }

        while (!_intervals.isEmpty()) {
            const LifeTimeInterval &i = _intervals.first();
            if (i.start() > _currentStmt->id)
                break;

            activate(i);

            _intervals.removeFirst();
        }
    }

    void cleanOldIntervals()
    {
        const int id = _currentStmt->id;
        QMutableHashIterator<Temp, LifeTimeInterval> it(_intervalForTemp);
        while (it.hasNext()) {
            const LifeTimeInterval &i = it.next().value();
            if (i.end() < id || i.isFixedInterval())
                it.remove();
        }
    }

    void resolve()
    {
        foreach (BasicBlock *bb, _function->basicBlocks) {
            foreach (BasicBlock *bbOut, bb->out) {
#ifdef DEBUG_REGALLOC
                Optimizer::showMeTheCode(_function);
#endif // DEBUG_REGALLOC

                resolveEdge(bb, bbOut);
            }
        }
    }

    class MoveMapping
    {
        struct Move {
            Expr *from;
            Temp *to;
            bool needsSwap;

            Move(Expr *from, Temp *to)
                : from(from), to(to), needsSwap(false)
            {}

            bool operator==(const Move &other) const
            { return from == other.from && to == other.to; }
        };

        QList<Move> _moves;

        int isUsedAsSource(Expr *e) const
        {
            if (Temp *t = e->asTemp())
                for (int i = 0, ei = _moves.size(); i != ei; ++i)
                    if (Temp *from = _moves[i].from->asTemp())
                        if (*from == *t)
                            return i;

            return -1;
        }

    public:
        void add(Expr *from, Temp *to) {
            if (Temp *t = from->asTemp())
                if (*t == *to)
                    return;

            Move m(from, to);
            if (_moves.contains(m))
                return;
            _moves.append(m);
        }

        void order()
        {
            QList<Move> todo = _moves;
            QList<Move> output;
            output.reserve(_moves.size());
            QList<Move> delayed;
            delayed.reserve(_moves.size());

            while (!todo.isEmpty()) {
                const Move m = todo.first();
                todo.removeFirst();
                schedule(m, todo, delayed, output);
            }

            Q_ASSERT(todo.isEmpty());
            Q_ASSERT(delayed.isEmpty());
            qSwap(_moves, output);
#if !defined(QT_NO_DEBUG)
            int swapCount = 0;
            foreach (const Move &m, _moves)
                if (m.needsSwap)
                    ++swapCount;
#endif
            Q_ASSERT(output.size() == _moves.size() + swapCount);
        }

#ifdef DEBUG_REGALLOC
        void dump() const
        {
            QTextStream os(stdout, QIODevice::WriteOnly);
            os << "Move mapping has " << _moves.size() << " moves..." << endl;
            foreach (const Move &m, _moves) {
                os << "\t";
                m.to->dump(os);
                if (m.needsSwap)
                    os << " <-> ";
                else
                    os << " <-- ";
                m.from->dump(os);
                os << endl;
            }
        }
#endif // DEBUG_REGALLOC

        void insertMoves(BasicBlock *predecessor, Function *function) const
        {
            int predecessorInsertionPoint = predecessor->statements.size() - 1;
            foreach (const Move &m, _moves) {
                V4IR::Move *move = function->New<V4IR::Move>();
                move->init(m.to, m.from, OpInvalid);
                move->swap = m.needsSwap;
                predecessor->statements.insert(predecessorInsertionPoint++, move);
            }
        }

    private:
        enum Action { NormalMove, NeedsSwap };
        Action schedule(const Move &m, QList<Move> &todo, QList<Move> &delayed, QList<Move> &output) const
        {
            int useIdx = isUsedAsSource(m.to);
            if (useIdx != -1) {
                const Move &dependency = _moves[useIdx];
                if (!output.contains(dependency)) {
                    if (delayed.contains(dependency)) {
                        // we have a cycle! Break it by using the scratch register
                        delayed+=m;
#ifdef DEBUG_REGALLOC
                        QTextStream out(stderr, QIODevice::WriteOnly);
                        out<<"we have a cycle! temps:" << endl;
                        foreach (const Move &m, delayed) {
                            out<<"\t";
                            m.to->dump(out);
                            out<<" <- ";
                            m.from->dump(out);
                            out<<endl;
                        }
#endif
                        delayed.removeOne(m);
                        return NeedsSwap;
                    } else {
                        delayed.append(m);
                        todo.removeOne(dependency);
                        Action action = schedule(dependency, todo, delayed, output);
                        delayed.removeOne(m);
                        Move mm(m);
                        mm.needsSwap = action == NeedsSwap;
                        output.append(mm);
                        return action;
                    }
                }
            }

            output.append(m);
            return NormalMove;
        }
    };

    void resolveEdge(BasicBlock *predecessor, BasicBlock *successor)
    {
#ifdef DEBUG_REGALLOC
        qDebug() << "Resolving edge" << predecessor->index << "->" << successor->index;
#endif // DEBUG_REGALLOC

        MoveMapping mapping;

        const int predecessorEnd = predecessor->statements.last()->id; // the terminator is always last and always has an id set...
        Q_ASSERT(predecessorEnd > 0); // ... but we verify it anyway for good measure.

        int successorStart = -1;
        foreach (Stmt *s, successor->statements) {
            if (s && s->id > 0) {
                successorStart = s->id;
                break;
            }
        }

        Q_ASSERT(successorStart > 0);

        foreach (const LifeTimeInterval &it, _liveAtStart[successor]) {
            if (it.end() < successorStart)
                continue;
            Expr *moveFrom = 0;
            if (it.start() == successorStart) {
                foreach (Stmt *s, successor->statements) {
                    if (!s || s->id < 1)
                        continue;
                    if (Phi *phi = s->asPhi()) {
                        if (*phi->targetTemp == it.temp()) {
                            Expr *opd = phi->d->incoming[successor->in.indexOf(predecessor)];
                            if (opd->asConst()) {
                                moveFrom = opd;
                            } else {
                                Temp *t = opd->asTemp();
                                Q_ASSERT(t);

                                foreach (const LifeTimeInterval &it2, _liveAtEnd[predecessor]) {
                                    if (it2.temp() == *t
                                            && it2.reg() != LifeTimeInterval::Invalid
                                            && it2.covers(predecessorEnd)) {
                                        moveFrom = createTemp(Temp::PhysicalRegister,
                                                              platformRegister(it2), t->type);
                                        break;
                                    }
                                }
                                if (!moveFrom)
                                    moveFrom = createTemp(Temp::StackSlot,
                                                          _assignedSpillSlots.value(*t, -1),
                                                          t->type);
                            }
                        }
                    } else {
                        break;
                    }
                }
            } else {
                foreach (const LifeTimeInterval &predIt, _liveAtEnd[predecessor]) {
                    if (predIt.temp() == it.temp()) {
                        if (predIt.reg() != LifeTimeInterval::Invalid
                                && predIt.covers(predecessorEnd)) {
                            moveFrom = createTemp(Temp::PhysicalRegister, platformRegister(predIt),
                                                  predIt.temp().type);
                        } else {
                            int spillSlot = _assignedSpillSlots.value(predIt.temp(), -1);
                            Q_ASSERT(spillSlot != -1);
                            moveFrom = createTemp(Temp::StackSlot, spillSlot, predIt.temp().type);
                        }
                        break;
                    }
                }
            }
            if (!moveFrom) {
                Q_ASSERT(!_info->isPhiTarget(it.temp()) || it.isSplitFromInterval());
#if !defined(QT_NO_DEBUG)
                if (_info->def(it.temp()) != successorStart && !it.isSplitFromInterval()) {
                    const int successorEnd = successor->statements.last()->id;
                    foreach (const Use &use, _info->uses(it.temp()))
                        Q_ASSERT(use.pos < successorStart || use.pos > successorEnd);
                }
#endif

                continue;
            }

            Temp *moveTo;
            if (it.reg() == LifeTimeInterval::Invalid || !it.covers(successorStart)) {
                int spillSlot = _assignedSpillSlots.value(it.temp(), -1);
                Q_ASSERT(spillSlot != -1); // TODO: check isStructurallyValidLanguageTag
                moveTo = createTemp(Temp::StackSlot, spillSlot, it.temp().type);
            } else {
                moveTo = createTemp(Temp::PhysicalRegister, platformRegister(it), it.temp().type);
            }

            // add move to mapping
            mapping.add(moveFrom, moveTo);
        }

        mapping.order();
#ifdef DEBUG_REGALLOC
        mapping.dump();
#endif // DEBUG_REGALLOC

        mapping.insertMoves(predecessor, _function);
    }

    Temp *createTemp(Temp::Kind kind, int index, Type type) const
    {
        Q_ASSERT(index >= 0);
        Temp *t = _function->New<Temp>();
        t->init(kind, index, 0);
        t->type = type;
        return t;
    }

    int platformRegister(const LifeTimeInterval &i) const
    {
        if (i.isFP())
            return _fpRegs.value(i.reg(), -1);
        else
            return _intRegs.value(i.reg(), -1);
    }

    Move *generateSpill(int spillSlot, Type type, int pReg) const
    {
        Q_ASSERT(spillSlot >= 0);

        Move *store = _function->New<Move>();
        store->init(createTemp(Temp::StackSlot, spillSlot, type),
                    createTemp(Temp::PhysicalRegister, pReg, type),
                    V4IR::OpInvalid);
        return store;
    }

    Move *generateUnspill(const Temp &t, int pReg) const
    {
        Q_ASSERT(pReg >= 0);
        int spillSlot = _assignedSpillSlots.value(t, -1);
        Q_ASSERT(spillSlot != -1);
        Move *load = _function->New<Move>();
        load->init(createTemp(Temp::PhysicalRegister, pReg, t.type),
                   createTemp(Temp::StackSlot, spillSlot, t.type),
                   V4IR::OpInvalid);
        return load;
    }

protected:
    virtual void visitTemp(Temp *t)
    {
        if (t->kind != Temp::VirtualRegister)
            return;

        const LifeTimeInterval &i = _intervalForTemp[*t];
        Q_ASSERT(i.isValid());
        if (i.reg() != LifeTimeInterval::Invalid && i.covers(_currentStmt->id)) {
            int pReg = platformRegister(i);
            t->kind = Temp::PhysicalRegister;
            t->index = pReg;
            Q_ASSERT(t->index >= 0);
        } else {
            int stackSlot = _assignedSpillSlots.value(*t, -1);
            Q_ASSERT(stackSlot >= 0);
            t->kind = Temp::StackSlot;
            t->index = stackSlot;
        }
    }

    virtual void visitConst(Const *) {}
    virtual void visitString(String *) {}
    virtual void visitRegExp(RegExp *) {}
    virtual void visitName(Name *) {}
    virtual void visitClosure(Closure *) {}
    virtual void visitConvert(Convert *e) { e->expr->accept(this); }
    virtual void visitUnop(Unop *e) { e->expr->accept(this); }
    virtual void visitBinop(Binop *e) { e->left->accept(this); e->right->accept(this); }
    virtual void visitSubscript(Subscript *e) { e->base->accept(this); e->index->accept(this); }
    virtual void visitMember(Member *e) { e->base->accept(this); }

    virtual void visitCall(Call *e) {
        e->base->accept(this);
        for (ExprList *it = e->args; it; it = it->next)
            it->expr->accept(this);
    }

    virtual void visitNew(New *e) {
        e->base->accept(this);
        for (ExprList *it = e->args; it; it = it->next)
            it->expr->accept(this);
    }

    virtual void visitExp(Exp *s) { s->expr->accept(this); }
    virtual void visitMove(Move *s) { s->source->accept(this); s->target->accept(this); }
    virtual void visitJump(Jump *) {}
    virtual void visitCJump(CJump *s) { s->cond->accept(this); }
    virtual void visitRet(Ret *s) { s->expr->accept(this); }
    virtual void visitTry(Try *) { Q_UNREACHABLE(); }
    virtual void visitPhi(Phi *) {}
};
} // anonymous namespace

RegisterAllocator::RegisterAllocator(const QVector<int> &normalRegisters, const QVector<int> &fpRegisters)
    : _normalRegisters(normalRegisters)
    , _fpRegisters(fpRegisters)
{
}

RegisterAllocator::~RegisterAllocator()
{
}

void RegisterAllocator::run(Function *function, const Optimizer &opt)
{
    _activeSpillSlots.resize(function->tempCount);

#ifdef DEBUG_REGALLOC
    qDebug() << "*** Running regalloc for function" << (function->name ? qPrintable(*function->name) : "NO NAME") << "***";
#endif // DEBUG_REGALLOC

    _unhandled = opt.lifeRanges();

    _info.reset(new RegAllocInfo);
    _info->collect(function);

#ifdef DEBUG_REGALLOC
    {
        QTextStream qout(stdout, QIODevice::WriteOnly);
        qout << "Ranges:" << endl;
        QList<LifeTimeInterval> handled = _unhandled;
        qSort(handled.begin(), handled.end(), LifeTimeInterval::lessThanForTemp);
        foreach (const LifeTimeInterval &r, handled) {
            r.dump(qout);
            qout << endl;
        }
    }
    _info->dump();
#endif // DEBUG_REGALLOC

    prepareRanges();

    _handled.reserve(_unhandled.size());
    _active.reserve(32);
    _inactive.reserve(16);

    Optimizer::showMeTheCode(function);

    linearScan();

#ifdef DEBUG_REGALLOC
    dump();
#endif // DEBUG_REGALLOC

    qSort(_handled.begin(), _handled.end(), LifeTimeInterval::lessThan);
    ResolutionPhase(_handled, function, _info.data(), _assignedSpillSlots, _normalRegisters, _fpRegisters).run();

    function->tempCount = QSet<int>::fromList(_assignedSpillSlots.values()).size();

    Optimizer::showMeTheCode(function);

#ifdef DEBUG_REGALLOC
    qDebug() << "*** Finished regalloc for function" << (function->name ? qPrintable(*function->name) : "NO NAME") << "***";
#endif // DEBUG_REGALLOC
}

static inline LifeTimeInterval createFixedInterval(int reg, bool isFP)
{
    Temp t;
    t.init(Temp::PhysicalRegister, reg, 0);
    t.type = isFP ? V4IR::DoubleType : V4IR::SInt32Type;
    LifeTimeInterval i;
    i.setTemp(t);
    i.setReg(reg);
    i.setFixedInterval(true);
    return i;
}

void RegisterAllocator::prepareRanges()
{
    const int regCount = _normalRegisters.size();
    _fixedRegisterRanges.resize(regCount);
    for (int reg = 0; reg < regCount; ++reg)
        _fixedRegisterRanges[reg] = createFixedInterval(reg, false);

    const int fpRegCount = _fpRegisters.size();
    _fixedFPRegisterRanges.resize(fpRegCount);
    for (int fpReg = 0; fpReg < fpRegCount; ++fpReg)
        _fixedFPRegisterRanges[fpReg] = createFixedInterval(fpReg, true);

    foreach (int callPosition, _info->calls()) {
        for (int reg = 0; reg < regCount; ++reg)
            _fixedRegisterRanges[reg].addRange(callPosition, callPosition);
        for (int fpReg = 0; fpReg < fpRegCount; ++fpReg)
            _fixedFPRegisterRanges[fpReg].addRange(callPosition, callPosition);
    }
    for (int reg = 0; reg < regCount; ++reg)
        if (_fixedRegisterRanges[reg].isValid())
            _active.append(_fixedRegisterRanges[reg]);
    for (int fpReg = 0; fpReg < fpRegCount; ++fpReg)
        if (_fixedFPRegisterRanges[fpReg].isValid())
            _active.append(_fixedFPRegisterRanges[fpReg]);

    qSort(_active.begin(), _active.end(), LifeTimeInterval::lessThan);
}

void RegisterAllocator::linearScan()
{
    while (!_unhandled.isEmpty()) {
        LifeTimeInterval current = _unhandled.first();
        _unhandled.removeFirst();
        int position = current.start();

        // check for intervals in active that are handled or inactive
        for (int i = 0; i < _active.size(); ) {
            const LifeTimeInterval &it = _active[i];
            if (it.end() < position) {
                if (!it.isFixedInterval())
                    _handled += it;
                _active.removeAt(i);
            } else if (!it.covers(position)) {
                _inactive += it;
                _active.removeAt(i);
            } else {
                ++i;
            }
        }

        // check for intervals in inactive that are handled or active
        for (int i = 0; i < _inactive.size(); ) {
            LifeTimeInterval &it = _inactive[i];
            if (it.end() < position) {
                if (!it.isFixedInterval())
                    _handled += it;
                _inactive.removeAt(i);
            } else if (it.covers(position)) {
                if (it.reg() != LifeTimeInterval::Invalid) {
                    _active += it;
                    _inactive.removeAt(i);
                } else {
                    // although this interval is now active, it has no register allocated (always
                    // spilled), so leave it in inactive.
                    ++i;
                }
            } else {
                ++i;
            }
        }

        Q_ASSERT(!current.isFixedInterval());

        if (_info->canHaveRegister(current.temp())) {
            tryAllocateFreeReg(current, position);
            if (current.reg() == LifeTimeInterval::Invalid)
                allocateBlockedReg(current, position);
            if (current.reg() != LifeTimeInterval::Invalid)
                _active += current;
        } else {
            assignSpillSlot(current.temp(), current.start(), current.end());
            _inactive += current;
#ifdef DEBUG_REGALLOC
            qDebug() << "*** allocating stack slot" << _assignedSpillSlots[current.temp()]
                     << "for %" << current.temp().index << "as it cannot be loaded in a register";
#endif // DEBUG_REGALLOC
        }
    }

    foreach (const LifeTimeInterval &r, _active)
        if (!r.isFixedInterval())
            _handled.append(r);
    _active.clear();
    foreach (const LifeTimeInterval &r, _inactive)
        if (!r.isFixedInterval())
            _handled.append(r);
    _inactive.clear();
}

static inline int indexOfRangeCoveringPosition(const LifeTimeInterval::Ranges &ranges, int position)
{
    for (int i = 0, ei = ranges.size(); i != ei; ++i) {
        if (position <= ranges[i].end)
            return i;
    }
    return -1;
}

static inline int intersectionPosition(const LifeTimeInterval::Range &one, const LifeTimeInterval::Range &two)
{
    if (one.covers(two.start))
        return two.start;
    if (two.covers(one.start))
        return one.start;
    return -1;
}

static inline bool isFP(const Temp &t)
{ return t.type == DoubleType; }

void RegisterAllocator::tryAllocateFreeReg(LifeTimeInterval &current, const int position)
{
    Q_ASSERT(!current.isFixedInterval());
    Q_ASSERT(current.reg() == LifeTimeInterval::Invalid);

    const bool needsFPReg = isFP(current.temp());
    QVector<int> freeUntilPos(needsFPReg ? _fpRegisters.size() : _normalRegisters.size(), INT_MAX);
    Q_ASSERT(freeUntilPos.size() > 0);

    const bool isPhiTarget = _info->isPhiTarget(current.temp());
    foreach (const LifeTimeInterval &it, _active) {
        if (it.isFP() == needsFPReg) {
            if (!isPhiTarget && it.isFixedInterval() && !current.isSplitFromInterval()) {
                const int idx = indexOfRangeCoveringPosition(it.ranges(), position);
                if (it.ranges().at(idx).end == current.start()) {
                    if (it.ranges().size() > idx + 1)
                        freeUntilPos[it.reg()] = it.ranges().at(idx + 1).start;
                    continue;
                }
            }

            if (isPhiTarget || it.end() >= current.firstPossibleUsePosition(isPhiTarget))
                freeUntilPos[it.reg()] = 0; // mark register as unavailable
        }
    }

    foreach (const LifeTimeInterval &it, _inactive) {
        if (current.isSplitFromInterval() || it.isFixedInterval()) {
            if (it.isFP() == needsFPReg && it.reg() != LifeTimeInterval::Invalid) {
                const int intersectionPos = nextIntersection(current, it, position);
                if (!isPhiTarget && it.isFixedInterval() && current.end() == intersectionPos)
                    freeUntilPos[it.reg()] = qMin(freeUntilPos[it.reg()], intersectionPos + 1);
                else if (intersectionPos != -1)
                    freeUntilPos[it.reg()] = qMin(freeUntilPos[it.reg()], intersectionPos);
            }
        }
    }

    int reg = LifeTimeInterval::Invalid;
    int freeUntilPos_reg = 0;

    foreach (const Temp &hint, _info->hints(current.temp())) {
        int candidate;
        if (hint.kind == Temp::PhysicalRegister)
            candidate = hint.index;
        else
            candidate = _lastAssignedRegister.value(hint, LifeTimeInterval::Invalid);

        const int end = current.end();
        if (candidate != LifeTimeInterval::Invalid) {
            if (current.isFP() == (hint.type == DoubleType)) {
                int fp = freeUntilPos[candidate];
                if ((freeUntilPos_reg < end && fp > freeUntilPos_reg)
                        || (freeUntilPos_reg >= end && fp >= end && freeUntilPos_reg > fp)) {
                    reg = candidate;
                    freeUntilPos_reg = fp;
                }
            }
        }
    }

    if (reg == LifeTimeInterval::Invalid)
        longestAvailableReg(freeUntilPos, reg, freeUntilPos_reg, current.end());

    if (freeUntilPos_reg == 0) {
        // no register available without spilling
#ifdef DEBUG_REGALLOC
        qDebug() << "*** no register available for %" << current.temp().index;
#endif // DEBUG_REGALLOC
        return;
    } else if (current.end() < freeUntilPos_reg) {
        // register available for the whole interval
#ifdef DEBUG_REGALLOC
        qDebug() << "*** allocating register" << reg << "for the whole interval of %" << current.temp().index;
#endif // DEBUG_REGALLOC
        current.setReg(reg);
        _lastAssignedRegister.insert(current.temp(), reg);
    } else {
        // register available for the first part of the interval
        current.setReg(reg);
        _lastAssignedRegister.insert(current.temp(), reg);
#ifdef DEBUG_REGALLOC
        qDebug() << "*** allocating register" << reg << "for the first part of interval of %" << current.temp().index;
#endif // DEBUG_REGALLOC
        split(current, freeUntilPos_reg, true);
    }
}

void RegisterAllocator::allocateBlockedReg(LifeTimeInterval &current, const int position)
{
    Q_ASSERT(!current.isFixedInterval());
    Q_ASSERT(current.reg() == LifeTimeInterval::Invalid);

    const bool needsFPReg = isFP(current.temp());
    QVector<int> nextUsePos(needsFPReg ? _fpRegisters.size() : _normalRegisters.size(), INT_MAX);
    QVector<LifeTimeInterval *> nextUseRangeForReg(nextUsePos.size(), 0);
    Q_ASSERT(nextUsePos.size() > 0);

    const bool isPhiTarget = _info->isPhiTarget(current.temp());
    for (int i = 0, ei = _active.size(); i != ei; ++i) {
        LifeTimeInterval &it = _active[i];
        if (it.isFP() == needsFPReg) {
            int nu = it.isFixedInterval() ? 0 : nextUse(it.temp(), current.firstPossibleUsePosition(isPhiTarget));
            if (nu != -1 && nu < nextUsePos[it.reg()]) {
                nextUsePos[it.reg()] = nu;
                nextUseRangeForReg[it.reg()] = &it;
            }
        }
    }

    for (int i = 0, ei = _inactive.size(); i != ei; ++i) {
        LifeTimeInterval &it = _inactive[i];
        if (current.isSplitFromInterval() || it.isFixedInterval()) {
            if (it.isFP() == needsFPReg && it.reg() != LifeTimeInterval::Invalid) {
                if (nextIntersection(current, it, position) != -1) {
                    int nu = nextUse(it.temp(), current.firstPossibleUsePosition(isPhiTarget));
                    if (nu != -1 && nu < nextUsePos[it.reg()]) {
                        nextUsePos[it.reg()] = nu;
                        nextUseRangeForReg[it.reg()] = &it;
                    }
                }
            }
        }
    }

    int reg, nextUsePos_reg;
    longestAvailableReg(nextUsePos, reg, nextUsePos_reg);

    if (current.start() > nextUsePos_reg) {
        // all other intervals are used before current, so it is best to spill current itself
        // Note: even if we absolutely need a register (e.g. with a floating-point add), we have
        // a scratch register available, and so we can still use a spill slot as the destination.
#ifdef DEBUG_REGALLOC
        QTextStream out(stderr, QIODevice::WriteOnly);
        out << "*** splitting current for range ";current.dump(out);out<<endl;
#endif // DEBUG_REGALLOC
        split(current, position + 1, true);
        _inactive.append(current);
    } else {
        // spill intervals that currently block reg
#ifdef DEBUG_REGALLOC
        QTextStream out(stderr, QIODevice::WriteOnly);
        out << "*** spilling intervals that block reg "<<reg<<" for interval ";current.dump(out);out<<endl;
#endif // DEBUG_REGALLOC
        current.setReg(reg);
        _lastAssignedRegister.insert(current.temp(), reg);
        Q_ASSERT(nextUseRangeForReg[reg]);
        split(*nextUseRangeForReg[reg], position);
        splitInactiveAtEndOfLifetimeHole(reg, needsFPReg, position);

        // make sure that current does not intersect with the fixed interval for reg
        const LifeTimeInterval &fixedRegRange = needsFPReg ? _fixedFPRegisterRanges[reg]
                                                           : _fixedRegisterRanges[reg];
        int ni = nextIntersection(current, fixedRegRange, position);
        if (ni != -1) {
#ifdef DEBUG_REGALLOC
            out << "***-- current range intersects with a fixed reg use at "<<ni<<", so splitting it."<<endl;
#endif // DEBUG_REGALLOC
            split(current, ni);
        }
    }
}

void RegisterAllocator::longestAvailableReg(const QVector<int> &nextUses, int &reg,
                                            int &nextUsePos_reg, int lastUse) const
{
    reg = 0;
    nextUsePos_reg = nextUses[reg];

    int bestReg = -1, nextUsePos_bestReg = INT_MAX;

    for (int i = 1, ei = nextUses.size(); i != ei; ++i) {
        int nextUsePos_i = nextUses[i];
        if (nextUsePos_i > nextUsePos_reg) {
            reg = i;
            nextUsePos_reg = nextUsePos_i;
        }
        if (lastUse != -1) {
            // if multiple registers are available for the whole life-time of an interval, then
            // bestReg contains the one that is blocked first another interval.
            if (nextUsePos_i > lastUse && nextUsePos_i < nextUsePos_bestReg) {
                bestReg = i;
                nextUsePos_bestReg = nextUsePos_i;
            }
        }
    }

    // if the hinted register is available for the whole life-time, use that one, and if not, use
    // the bestReg.
    if (bestReg != -1) {
        reg = bestReg;
        nextUsePos_reg = nextUsePos_bestReg;
    }
}

int RegisterAllocator::nextIntersection(const LifeTimeInterval &current,
                                        const LifeTimeInterval &another, const int position) const
{
    LifeTimeInterval::Ranges currentRanges = current.ranges();
    int currentIt = indexOfRangeCoveringPosition(currentRanges, position);
    if (currentIt == -1)
        return -1;

    LifeTimeInterval::Ranges anotherRanges = another.ranges();
    int anotherIt = indexOfRangeCoveringPosition(anotherRanges, position);
    if (anotherIt == -1)
        return -1;

    for (int currentEnd = currentRanges.size(); currentIt < currentEnd; ++currentIt) {
        const LifeTimeInterval::Range &currentRange = currentRanges[currentIt];
        for (int anotherEnd = anotherRanges.size(); anotherIt < anotherEnd; ++anotherIt) {
            int intersectPos = intersectionPosition(currentRange, anotherRanges[anotherIt]);
            if (intersectPos != -1)
                return intersectPos;
        }
    }

    return -1;
}

int RegisterAllocator::nextUse(const Temp &t, int startPosition) const
{
    QList<Use> usePositions = _info->uses(t);
    for (int i = 0, ei = usePositions.size(); i != ei; ++i) {
        int usePos = usePositions[i].pos;
        if (usePos >= startPosition)
            return usePos;
    }

    return -1;
}

static inline void insertSorted(QList<LifeTimeInterval> &intervals, const LifeTimeInterval &newInterval)
{
    for (int i = 0, ei = intervals.size(); i != ei; ++i) {
        if (LifeTimeInterval::lessThan(newInterval, intervals.at(i))) {
            intervals.insert(i, newInterval);
            return;
        }
    }
    intervals.append(newInterval);
}

void RegisterAllocator::split(LifeTimeInterval &current, int beforePosition,
                              bool skipOptionalRegisterUses)
{ // TODO: check if we can always skip the optional register uses
    Q_ASSERT(!current.isFixedInterval());

#ifdef DEBUG_REGALLOC
    QTextStream out(stderr, QIODevice::WriteOnly);
    out << "***** split request for range ";current.dump(out);out<<" before position "<<beforePosition<<" and skipOptionalRegisterUses = "<<skipOptionalRegisterUses<<endl;
#endif // DEBUG_REGALLOC

    assignSpillSlot(current.temp(), current.start(), current.end());

    const int defPosition = _info->def(current.temp());
    if (beforePosition < defPosition) {
#ifdef DEBUG_REGALLOC
        out << "***** split before position is before or at definition, so not splitting."<<endl;
#endif // DEBUG_REGALLOC
        return;
    }

    int lastUse = -1;
    if (defPosition < beforePosition)
        lastUse = defPosition;
    int nextUse = -1;
    QList<Use> usePositions = _info->uses(current.temp());
    for (int i = 0, ei = usePositions.size(); i != ei; ++i) {
        const Use &usePosition = usePositions[i];
        const int usePos = usePosition.pos;
        if (lastUse < usePos && usePos < beforePosition) {
            lastUse = usePos;
        } else if (usePos >= beforePosition) {
            if (!skipOptionalRegisterUses || usePosition.mustHaveRegister()) {
                nextUse = usePos;
                break;
            }
        }
    }
    if (lastUse == -1)
        lastUse = beforePosition - 1;

    Q_ASSERT(lastUse < beforePosition);

#ifdef DEBUG_REGALLOC
    out << "***** last use = "<<lastUse<<", nextUse = " << nextUse<<endl;
#endif // DEBUG_REGALLOC
    LifeTimeInterval newInterval = current.split(lastUse, nextUse);
#ifdef DEBUG_REGALLOC
    out << "***** new interval: "; newInterval.dump(out); out << endl;
    out << "***** preceding interval: "; current.dump(out); out << endl;
#endif // DEBUG_REGALLOC
    if (newInterval.isValid()) {
        if (current.reg() != LifeTimeInterval::Invalid)
            _info->addHint(current.temp(), current.reg());
        newInterval.setReg(LifeTimeInterval::Invalid);
        insertSorted(_unhandled, newInterval);
    }
}

void RegisterAllocator::splitInactiveAtEndOfLifetimeHole(int reg, bool isFPReg, int position)
{
    for (int i = 0, ei = _inactive.size(); i != ei; ++i) {
        LifeTimeInterval &interval = _inactive[i];
        if (isFPReg == interval.isFP() && interval.reg() == reg) {
            LifeTimeInterval::Ranges ranges = interval.ranges();
            int endOfLifetimeHole = -1;
            for (int j = 0, ej = ranges.size(); j != ej; ++j) {
                if (position < ranges[j].start)
                    endOfLifetimeHole = ranges[j].start;
            }
            if (endOfLifetimeHole != -1)
                split(interval, endOfLifetimeHole);
        }
    }
}

void RegisterAllocator::assignSpillSlot(const Temp &t, int startPos, int endPos)
{
    if (_assignedSpillSlots.contains(t))
        return;

    for (int i = 0, ei = _activeSpillSlots.size(); i != ei; ++i) {
        if (_activeSpillSlots[i] < startPos) {
            _activeSpillSlots[i] = endPos;
            _assignedSpillSlots.insert(t, i);
            return;
        }
    }

    Q_UNREACHABLE();
}

void RegisterAllocator::dump() const
{
#ifdef DEBUG_REGALLOC
    QTextStream qout(stdout, QIODevice::WriteOnly);

    {
        qout << "Ranges:" << endl;
        QList<LifeTimeInterval> handled = _handled;
        qSort(handled.begin(), handled.end(), LifeTimeInterval::lessThanForTemp);
        foreach (const LifeTimeInterval &r, handled) {
            r.dump(qout);
            qout << endl;
        }
    }

    {
        qout << "Spill slots:" << endl;
        QList<Temp> temps = _assignedSpillSlots.keys();
        if (temps.isEmpty())
            qout << "\t(none)" << endl;
        qSort(temps);
        foreach (const Temp &t, temps) {
            qout << "\t";
            t.dump(qout);
            qout << " -> " << _assignedSpillSlots[t] << endl;
        }
    }
#endif // DEBUG_REGALLOC
}