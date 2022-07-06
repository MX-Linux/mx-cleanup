# **********************************************************************
# * Copyright (C) 2018 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with this package. If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

QT       += core gui widgets
CONFIG   += c++1z

TARGET = mx-cleanup
TEMPLATE = app

# The following define makes your compiler warn you if you use any
# feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += main.cpp\
    mainwindow.cpp

HEADERS  += \
    mainwindow.h \
    version.h

FORMS    += \
    mainwindow.ui

TRANSLATIONS += translations/mx-cleanup_am.ts \
                translations/mx-cleanup_ar.ts \
                translations/mx-cleanup_bg.ts \
                translations/mx-cleanup_ca.ts \
                translations/mx-cleanup_cs.ts \
                translations/mx-cleanup_da.ts \
                translations/mx-cleanup_de.ts \
                translations/mx-cleanup_el.ts \
                translations/mx-cleanup_en.ts \
                translations/mx-cleanup_es.ts \
                translations/mx-cleanup_et.ts \
                translations/mx-cleanup_eu.ts \
                translations/mx-cleanup_fa.ts \
                translations/mx-cleanup_fi.ts \
                translations/mx-cleanup_fr.ts \
                translations/mx-cleanup_he_IL.ts \
                translations/mx-cleanup_hi.ts \
                translations/mx-cleanup_hr.ts \
                translations/mx-cleanup_hu.ts \
                translations/mx-cleanup_id.ts \
                translations/mx-cleanup_is.ts \
                translations/mx-cleanup_it.ts \
                translations/mx-cleanup_ja.ts \
                translations/mx-cleanup_kk.ts \
                translations/mx-cleanup_ko.ts \
                translations/mx-cleanup_lt.ts \
                translations/mx-cleanup_mk.ts \
                translations/mx-cleanup_mr.ts \
                translations/mx-cleanup_nb.ts \
                translations/mx-cleanup_nl.ts \
                translations/mx-cleanup_pl.ts \
                translations/mx-cleanup_pt.ts \
                translations/mx-cleanup_pt_BR.ts \
                translations/mx-cleanup_ro.ts \
                translations/mx-cleanup_ru.ts \
                translations/mx-cleanup_sk.ts \
                translations/mx-cleanup_sl.ts \
                translations/mx-cleanup_sq.ts \
                translations/mx-cleanup_sr.ts \
                translations/mx-cleanup_sv.ts \
                translations/mx-cleanup_tr.ts \
                translations/mx-cleanup_uk.ts \
                translations/mx-cleanup_zh_CN.ts \
                translations/mx-cleanup_zh_TW.ts

RESOURCES += \
    images.qrc

