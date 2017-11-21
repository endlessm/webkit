/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

WI.TreeOutlineGroup = class TreeOutlineGroup extends WI.Collection
{
    constructor()
    {
        super((object) => object instanceof WI.TreeOutline);
    }

    // Static

    static groupForTreeOutline(treeOutline)
    {
        return treeOutline[WI.TreeOutlineGroup.GroupForTreeOutlineSymbol] || null;
    }

    // Public

    get selectedTreeElement()
    {
        for (let treeOutline of this.items) {
            if (treeOutline.selectedTreeElement)
                return treeOutline.selectedTreeElement;
        }

        return null;
    }

    // Protected

    itemAdded(treeOutline)
    {
        console.assert(!treeOutline[WI.TreeOutlineGroup.GroupForTreeOutlineSymbol]);
        treeOutline[WI.TreeOutlineGroup.GroupForTreeOutlineSymbol] = this;

        if (treeOutline.selectedTreeElement)
            this._removeConflictingTreeSelections(treeOutline.selectedTreeElement);
    }

    itemRemoved(treeOutline)
    {
        console.assert(treeOutline[WI.TreeOutlineGroup.GroupForTreeOutlineSymbol] === this);
        treeOutline[WI.TreeOutlineGroup.GroupForTreeOutlineSymbol] = null;
    }

    didSelectTreeElement(treeElement)
    {
        // Called by TreeOutline.

        if (!treeElement)
            return;

        this._removeConflictingTreeSelections(treeElement);
    }

    // Private

    _removeConflictingTreeSelections(treeElement)
    {
        let selectedTreeOutline = treeElement.treeOutline;
        console.assert(selectedTreeOutline, "Should have a parent tree outline.");

        for (let treeOutline of this.items) {
            if (selectedTreeOutline === treeOutline)
                continue;

            if (treeOutline.selectedTreeElement)
                treeOutline.selectedTreeElement.deselect();
        }
    }
};

WI.TreeOutlineGroup.GroupForTreeOutlineSymbol = Symbol("group-for-tree-outline");
