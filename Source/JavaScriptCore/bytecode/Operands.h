/*
 * Copyright (C) 2011, 2012, 2013, 2015, 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "CallFrame.h"
#include "VirtualRegister.h"

#include <wtf/PrintStream.h>
#include <wtf/Vector.h>

namespace JSC {

template<typename T> struct OperandValueTraits;

enum OperandKind { ArgumentOperand, LocalOperand };

enum OperandsLikeTag { OperandsLike };

template<typename T>
class Operands {
public:
    Operands() { }
    
    explicit Operands(size_t numArguments, size_t numLocals)
    {
        if (WTF::VectorTraits<T>::needsInitialization) {
            m_arguments.resize(numArguments);
            m_locals.resize(numLocals);
        } else {
            m_arguments.fill(T(), numArguments);
            m_locals.fill(T(), numLocals);
        }
    }

    explicit Operands(size_t numArguments, size_t numLocals, const T& initialValue)
    {
        m_arguments.fill(initialValue, numArguments);
        m_locals.fill(initialValue, numLocals);
    }
    
    template<typename U>
    explicit Operands(OperandsLikeTag, const Operands<U>& other)
    {
        m_arguments.fill(T(), other.numberOfArguments());
        m_locals.fill(T(), other.numberOfLocals());
    }
    
    size_t numberOfArguments() const { return m_arguments.size(); }
    size_t numberOfLocals() const { return m_locals.size(); }
    
    T& argument(size_t idx) { return m_arguments[idx]; }
    const T& argument(size_t idx) const { return m_arguments[idx]; }
    
    T& local(size_t idx) { return m_locals[idx]; }
    const T& local(size_t idx) const { return m_locals[idx]; }
    
    template<OperandKind operandKind>
    size_t sizeFor() const
    {
        if (operandKind == ArgumentOperand)
            return numberOfArguments();
        return numberOfLocals();
    }
    template<OperandKind operandKind>
    T& atFor(size_t idx)
    {
        if (operandKind == ArgumentOperand)
            return argument(idx);
        return local(idx);
    }
    template<OperandKind operandKind>
    const T& atFor(size_t idx) const
    {
        if (operandKind == ArgumentOperand)
            return argument(idx);
        return local(idx);
    }
    
    void ensureLocals(size_t size)
    {
        size_t oldSize = m_locals.size();
        if (size <= oldSize)
            return;

        m_locals.grow(size);
        if (!WTF::VectorTraits<T>::needsInitialization) {
            for (size_t i = oldSize; i < m_locals.size(); ++i)
                m_locals[i] = T();
        }
    }

    void ensureLocals(size_t size, const T& ensuredValue)
    {
        size_t oldSize = m_locals.size();
        if (size <= oldSize)
            return;

        m_locals.grow(size);
        for (size_t i = oldSize; i < m_locals.size(); ++i)
            m_locals[i] = ensuredValue;
    }
    
    void setLocal(size_t idx, const T& value)
    {
        ensureLocals(idx + 1);
        
        m_locals[idx] = value;
    }
    
    T getLocal(size_t idx)
    {
        if (idx >= m_locals.size())
            return T();
        return m_locals[idx];
    }
    
    void setArgumentFirstTime(size_t idx, const T& value)
    {
        ASSERT(m_arguments[idx] == T());
        argument(idx) = value;
    }
    
    void setLocalFirstTime(size_t idx, const T& value)
    {
        ASSERT(idx >= m_locals.size() || m_locals[idx] == T());
        setLocal(idx, value);
    }
    
    T& operand(int operand)
    {
        if (operandIsArgument(operand)) {
            int argument = VirtualRegister(operand).toArgument();
            return m_arguments[argument];
        }

        return m_locals[VirtualRegister(operand).toLocal()];
    }

    T& operand(VirtualRegister virtualRegister)
    {
        return operand(virtualRegister.offset());
    }

    const T& operand(int operand) const { return const_cast<const T&>(const_cast<Operands*>(this)->operand(operand)); }
    const T& operand(VirtualRegister operand) const { return const_cast<const T&>(const_cast<Operands*>(this)->operand(operand)); }
    
    bool hasOperand(int operand) const
    {
        if (operandIsArgument(operand))
            return true;
        return static_cast<size_t>(VirtualRegister(operand).toLocal()) < numberOfLocals();
    }
    bool hasOperand(VirtualRegister reg) const
    {
        return hasOperand(reg.offset());
    }
    
    void setOperand(int operand, const T& value)
    {
        if (operandIsArgument(operand)) {
            int argument = VirtualRegister(operand).toArgument();
            m_arguments[argument] = value;
            return;
        }
        
        setLocal(VirtualRegister(operand).toLocal(), value);
    }
    
    void setOperand(VirtualRegister virtualRegister, const T& value)
    {
        setOperand(virtualRegister.offset(), value);
    }

    size_t size() const { return numberOfArguments() + numberOfLocals(); }
    const T& at(size_t index) const
    {
        if (index < numberOfArguments())
            return m_arguments[index];
        return m_locals[index - numberOfArguments()];
    }
    T& at(size_t index)
    {
        if (index < numberOfArguments())
            return m_arguments[index];
        return m_locals[index - numberOfArguments()];
    }
    const T& operator[](size_t index) const { return at(index); }
    T& operator[](size_t index) { return at(index); }

    bool isArgument(size_t index) const { return index < numberOfArguments(); }
    bool isVariable(size_t index) const { return !isArgument(index); }
    int argumentForIndex(size_t index) const
    {
        return index;
    }
    int variableForIndex(size_t index) const
    {
        return index - m_arguments.size();
    }
    int operandForIndex(size_t index) const
    {
        if (index < numberOfArguments())
            return virtualRegisterForArgument(index).offset();
        return virtualRegisterForLocal(index - numberOfArguments()).offset();
    }
    VirtualRegister virtualRegisterForIndex(size_t index) const
    {
        return VirtualRegister(operandForIndex(index));
    }
    size_t indexForOperand(int operand) const
    {
        if (operandIsArgument(operand))
            return static_cast<size_t>(VirtualRegister(operand).toArgument());
        return static_cast<size_t>(VirtualRegister(operand).toLocal()) + numberOfArguments();
    }
    size_t indexForOperand(VirtualRegister reg) const
    {
        return indexForOperand(reg.offset());
    }
    
    void setOperandFirstTime(int operand, const T& value)
    {
        if (operandIsArgument(operand)) {
            setArgumentFirstTime(VirtualRegister(operand).toArgument(), value);
            return;
        }
        
        setLocalFirstTime(VirtualRegister(operand).toLocal(), value);
    }
    
    void fill(T value)
    {
        for (size_t i = 0; i < m_arguments.size(); ++i)
            m_arguments[i] = value;
        for (size_t i = 0; i < m_locals.size(); ++i)
            m_locals[i] = value;
    }
    
    void clear()
    {
        fill(T());
    }
    
    bool operator==(const Operands& other) const
    {
        ASSERT(numberOfArguments() == other.numberOfArguments());
        ASSERT(numberOfLocals() == other.numberOfLocals());
        
        return m_arguments == other.m_arguments && m_locals == other.m_locals;
    }
    
    void dumpInContext(PrintStream& out, DumpContext* context) const;
    void dump(PrintStream& out) const;
    
private:
    Vector<T, 8> m_arguments;
    Vector<T, 16> m_locals;
};

} // namespace JSC
