/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 zhang yu <zhangyud@uniontech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "splitswap.h"

namespace SplitConsts {
    const QEasingCurve TOGGLE_MODE =  QEasingCurve::OutExpo;// AnimationMode.EASE_OUT_Expo;
    static const int FADE_DURATION = 600;
    static const qreal REBOUND_COEF = 0.85;
}

namespace KWin
{
SplitSwapEffect::SplitSwapEffect()
    :Effect()
{
    reconfigure(ReconfigureAll);
    connect(effectsEx, &EffectsHandlerEx::swapSplitWin, this, &SplitSwapEffect::onSwapWindow);
}

SplitSwapEffect::~SplitSwapEffect()
{
}

void SplitSwapEffect::reconfigure(ReconfigureFlags flags)
{
    m_duration = std::chrono::milliseconds(static_cast<int>(animationTime(SplitConsts::FADE_DURATION)));
    m_animationTime.setDuration(m_duration);
    m_animationTime.setDirection(TimeLine::Forward);
    m_animationTime.setEasingCurve(SplitConsts::TOGGLE_MODE);
}

void SplitSwapEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (isActive()) {
        m_animationTime.advance(presentTime);
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }
    for (auto const& w: effects->stackingOrder()) {
        w->setData(WindowForceBlurRole, QVariant(true));
    }
    effects->prePaintScreen(data, presentTime);
}

void SplitSwapEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData &data)
{
    effects->paintScreen(mask, region, data);
}

void SplitSwapEffect::postPaintScreen()
{
    effects->postPaintScreen();
    if (m_isFinish) {
        if (m_activated && m_animationTime.done()) {
            for (auto const& w: effects->stackingOrder()) {
                if (w->screen() != m_dragScreen)
                    continue;
                if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Left)) {
                    resetWinPos(w, QuickTileMode(m_leftMode));
                    effectsEx->updateQuickTileMode(w, m_leftMode);
                } else if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Right)) {
                    resetWinPos(w, QuickTileMode(m_rightMode));
                    effectsEx->updateQuickTileMode(w, m_rightMode);
                }
            }
            if (m_leftMode != int(QuickTileFlag::Left))
                effectsEx->updateWindowTile(m_dragScreen);
            setActive(false);
        }
    }
    for (auto const& w: effects->stackingOrder()) {
        w->setData(WindowForceBlurRole, QVariant());
    }
    if (m_activated && m_animationTime.running()) {
        effects->addRepaintFull();
    }
}

void SplitSwapEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    data.setTransformed();
    effects->prePaintWindow(w, data, presentTime);
}

void SplitSwapEffect::paintWindow(EffectWindow *w, int mask, QRegion region, WindowPaintData &data)
{
    if (!isActive() || w->screen() != m_dragScreen) {
        effects->paintWindow(w, mask, region, data);
        return;
    }

    if (w->isDesktop()) {
        data.setBrightness(0.8);
    }
    if (m_isSwap) {
        int xPos = 0;
        if (w != m_dragEffectWin ) {
            if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Left)) {
                xPos = paintWinPos(w, QuickTileMode(m_leftMode), 2);
                data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
            } else if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Right)) {
                xPos = paintWinPos(w, QuickTileMode(m_rightMode), 2);
                data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
            }
        }
    }
    if (m_isFinish) {
        int xPos = 0;
        if (w == m_dragEffectWin) {
            if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Left)) {
                xPos = paintWinPos(w, QuickTileMode(m_leftMode), 0);
                data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
            } else if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Right)) {
                xPos = paintWinPos(w, QuickTileMode(m_rightMode), 0);
                data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
            }
        } else if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Left)) {
            xPos = paintWinPos(w, QuickTileMode(m_leftMode), 1);
            data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
        } else if (effectsEx->getQuickTileMode(w) == QuickTileMode(QuickTileFlag::Right)) {
            xPos = paintWinPos(w, QuickTileMode(m_rightMode), 1);
            data += QPoint(xPos - w->x(), m_workarea.y() - w->y());
        }
    }
    effects->paintWindow(w, mask, region, data);
}

bool SplitSwapEffect::isActive() const
{
    return m_activated;
}

void SplitSwapEffect::setActive(bool active)
{
    if (m_activated == active)
        return;
    m_activated = active;
    if (active) {
        effects->setActiveFullScreenEffect(this);
    } else {
        m_isSwap = false;
        m_leftMode = int(QuickTileFlag::Left);
        m_rightMode = int(QuickTileFlag::Right);
        m_dragEffectWin = nullptr;
        m_dragScreen = nullptr;
        effects->setActiveFullScreenEffect(0);
    }
    effects->addRepaintFull();
}

void SplitSwapEffect::onSwapWindow(EffectWindow *w, int index)
{
    m_animationTime.reset();
    if (index == 0) {           //quit split state
        m_animationTime.setElapsed(m_duration);
        m_isSwap = false;
        m_isFinish = true;
        return;
    } else if (index == 2) {    //finish split swap
        m_isSwap = false;
        m_isFinish = true;
        m_animationTime.setElapsed(std::chrono::milliseconds(1));
        effects->addRepaintFull();
    } else if (index == 1) {    //swaping
        m_isSwap = true;
        m_isFinish = false;
        m_leftMode = m_leftMode ^ 0b11;
        m_rightMode = m_rightMode ^ 0b11;
    }

    m_dragEffectWin = w;
    if (!m_dragScreen)
        m_dragScreen = w->screen();

    m_workarea = effects->clientArea(MaximizeArea, w).toRect();
    setActive(true);
}

bool SplitSwapEffect::isRelevantWithPresentWindows(EffectWindow *w) const
{
    if (w->isSpecialWindow() || w->isUtility()) {
        return false;
    }
    if (w->isDock()) {
        return false;
    }
    if (w->isSkipSwitcher()) {
        return false;
    }
    if (w->isDeleted()) {
        return false;
    }
    if (!w->acceptsFocus()) {
        return false;
    }
    if (!w->isCurrentTab()) {
        return false;
    }
    if (!w->isOnCurrentActivity()) {
        return false;
    }
    return true;
}

void SplitSwapEffect::resetWinPos(EffectWindow *w, QuickTileMode mode)
{
    if (mode & QuickTileFlag::Left && w->frameGeometry().topLeft() != QPoint(m_workarea.x(), m_workarea.y())) {
        effects->moveWindow(w, QPoint(m_workarea.x(), m_workarea.y()));
    } else if (mode & QuickTileFlag::Right && w->frameGeometry().topLeft() != QPoint(m_workarea.x() + m_workarea.width() - w->width(), m_workarea.y())) {
        effects->moveWindow(w, QPoint(m_workarea.x() + m_workarea.width() - w->width(), m_workarea.y()));
    }
}

int SplitSwapEffect::paintWinPos(EffectWindow *w, QuickTileMode mode, int calculationMethod)
{
    if (nullptr == w) {
        return 0;
    }
    qreal coef = m_animationTime.value();
    int tpos = 0;
    int epos = 0;
    int x = 0;

    switch (calculationMethod)
    {
    case 0:
        x = (mode & QuickTileFlag::Left) ? w->x() : x = w->x();
        epos = m_workarea.x() + m_workarea.width() - w->width() - w->x();
        break;
    case 1:
        coef = 1.0;
    case 2:
        x = (mode & QuickTileFlag::Left) ? m_workarea.x() + m_workarea.width() - w->width() : m_workarea.x();
        epos = m_workarea.width() - w->width();
        break;
    }

    if (mode & QuickTileFlag::Left) {
        tpos = x - (x - m_workarea.x()) * coef;
    } else {
        tpos = x + epos * coef;
    }
    return  tpos;
}

}