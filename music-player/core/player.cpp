/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include "player.h"

#include "playlist.h"

#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QMimeDatabase>

static QMap<QString, bool>  sSupportedSuffix;
static QStringList          sSupportedSuffixList;
static QStringList          sSupportedFiterList;
static QStringList          sSupportedMimeTypes;

void initMiniTypes()
{
    //black list
    QHash<QString, bool> suffixBlacklist;
    suffixBlacklist.insert("m3u", true);

    QHash<QString, bool> suffixWhitelist;
    suffixWhitelist.insert("cue", true);

    QMimeDatabase mdb;
    for (auto &mt : mdb.allMimeTypes()) {
        if (mt.name().startsWith("audio/")) {
            sSupportedFiterList << mt.filterString();
            for (auto &suffix : mt.suffixes()) {
                if (suffixBlacklist.contains(suffix)) {
                    continue;
                }

                sSupportedSuffixList << "*." + suffix;
                sSupportedSuffix.insert(suffix, true);
            }
            sSupportedMimeTypes << mt.name();
        }
    }

    for (auto &suffix : suffixWhitelist.keys()) {
        sSupportedSuffixList << "*." + suffix;
        sSupportedSuffix.insert(suffix, true);
    }
}

QStringList Player::supportedFilterStringList()
{
    return sSupportedFiterList;
}

QStringList Player::supportedMimeTypes()
{
    return sSupportedMimeTypes;
}

const MusicMeta Player::playingMeta()
{
    return m_playingMeta;
}

void Player::setPlaylist(PlaylistPtr /*playlist*/)
{
}

void Player::setMode(Player::PlayMode mode)
{
    m_mode = mode;
}

void Player::playMusic(PlaylistPtr playlist, const MusicMeta &info)
{
    MusicMeta nextInfo = info;

    m_playinglist = playlist;
    if (info.hash.isEmpty() && this->state() == QMediaPlayer::State::StoppedState) {
        nextInfo = m_playinglist->first();
    }

    this->blockSignals(true);
    qDebug() << nextInfo.title;
    setMediaMeta(nextInfo);
    this->blockSignals(false);

    // TODO: fixme wait media ready;

    this->setPosition(nextInfo.offset);

    QTimer::singleShot(100, this, [ = ]() {
        this->play();
    });
    m_playinglist->play(nextInfo);
    emit musicPlayed(playlist, nextInfo);
}

void Player::resumeMusic(PlaylistPtr playlist, const MusicMeta &meta)
{
    Q_ASSERT(playlist == m_playinglist);
    Q_ASSERT(meta.hash == m_playingMeta.hash);

    QTimer::singleShot(50, this, [ = ]() {
        this->play();
    });
}

void Player::playNextMusic(PlaylistPtr playlist, const MusicMeta &info)
{
    Q_ASSERT(playlist == m_playinglist);

    if (m_mode == RepeatSingle) {
        selectNext(info, RepeatAll);
    } else {
        selectNext(info, m_mode);
    }
}

void Player::playPrevMusic(PlaylistPtr playlist, const MusicMeta &info)
{
    Q_ASSERT(playlist == m_playinglist);

    if (m_mode == RepeatSingle) {
        selectPrev(info, RepeatAll);
    } else {
        selectPrev(info, m_mode);
    }
}

void Player::setMediaMeta(const MusicMeta &info)
{
    if (this->media().canonicalUrl() != QUrl::fromLocalFile(info.localPath)) {
        QMediaPlayer::setMedia(QUrl::fromLocalFile(info.localPath));
    }
    m_playingMeta = info;
}

void Player::changeProgress(qint64 value, qint64 range)
{
    Q_ASSERT(value <= range);
    //Q_ASSERT(m_info.offset + m_info.length < QMediaPlayer::duration());

    auto position = value * m_playingMeta.length / range + m_playingMeta.offset;
    if (position < 0) {
        qCritical() << "invaild position:" << this->media().canonicalUrl() << position;
        return;
    }
    this->setPosition(position);
}

void Player::selectNext(const MusicMeta &info, PlayMode mode)
{
    if (!m_playinglist) {
        return;
    }

    switch (mode) {
    case RepeatAll: {
        playMusic(m_playinglist, m_playinglist->next(info));
        break;
    }
    case RepeatSingle: {
        playMusic(m_playinglist, info);
        break;
    }
    case Shuffle: {
        int randomValue = qrand() % m_playinglist->length();
        playMusic(m_playinglist, m_playinglist->music(randomValue));
        break;
    }
    }
}

void Player::selectPrev(const MusicMeta &info, Player::PlayMode mode)
{
    if (!m_playinglist) {
        return;
    }

    switch (mode) {
    case RepeatAll: {
        playMusic(m_playinglist, m_playinglist->prev(info));
        break;
    }
    case RepeatSingle: {
        playMusic(m_playinglist, info);
        break;
    }
    case Shuffle: {
        int randomValue = qrand() % m_playinglist->length();
        playMusic(m_playinglist, m_playinglist->music(randomValue));
        break;
    }
    }
}

Player::Player(QObject *parent) : QMediaPlayer(parent)
{
    initMiniTypes();
    connect(this, &QMediaPlayer::durationChanged, this, [ = ](qint64 duration) {
        m_duration = duration;
    });
    connect(this, &QMediaPlayer::positionChanged, this, [ = ](qint64 position) {
        if (0 == m_playingMeta.length) {
            return;
        }

        if (m_playingMeta.offset > position) {
            return;
        }

//        qDebug() << position << m_playingMeta.offset << m_playingMeta.length;
        if (position >= m_playingMeta.offset + m_playingMeta.length) {
            // TODO: to next
//            qDebug() << "auto change next music" << m_info.title;
//            qDebug() << lengthString(m_duration)
//                     << lengthString(position)
//                     << lengthString(m_info.offset)
//                     << lengthString(m_info.length) ;

            this->selectNext(m_playingMeta, m_mode);
        }
//        qDebug() << lengthString(m_duration)
//                 << lengthString(position)
//                 << lengthString(m_info.offset)
//                 << lengthString(m_info.length) ;
        emit progrossChanged(position - m_playingMeta.offset,  m_playingMeta.length);
    });
    connect(this, &QMediaPlayer::stateChanged, this, [ = ](QMediaPlayer::State state) {
        switch (state) {
        case QMediaPlayer::StoppedState: {
//            qDebug() << "auto change next music";
//            this->selectNext(m_info, m_mode);
//            break;
        }
        case QMediaPlayer::PlayingState:
        case QMediaPlayer::PausedState:
            break;
        }
    });
}