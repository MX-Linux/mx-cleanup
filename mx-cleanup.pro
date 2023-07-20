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
    about.cpp \
    mainwindow.cpp

HEADERS  += \
    about.h \
    mainwindow.h \
    version.h

FORMS    += \
    mainwindow.ui

TRANSLATIONS += translations/mx-cleanup_af.ts \
                translations/mx-cleanup_am.ts \
                translations/mx-cleanup_ar.ts \
                translations/mx-cleanup_ast.ts \
                translations/mx-cleanup_be.ts \
                translations/mx-cleanup_bg.ts \
                translations/mx-cleanup_bn.ts \
                translations/mx-cleanup_bs_BA.ts \
                translations/mx-cleanup_bs.ts \
                translations/mx-cleanup_ca.ts \
                translations/mx-cleanup_ceb.ts \
                translations/mx-cleanup_co.ts \
                translations/mx-cleanup_cs.ts \
                translations/mx-cleanup_cy.ts \
                translations/mx-cleanup_da.ts \
                translations/mx-cleanup_de.ts \
                translations/mx-cleanup_el.ts \
                translations/mx-cleanup_en_GB.ts \
                translations/mx-cleanup_en.ts \
                translations/mx-cleanup_en_US.ts \
                translations/mx-cleanup_eo.ts \
                translations/mx-cleanup_es_ES.ts \
                translations/mx-cleanup_es.ts \
                translations/mx-cleanup_et.ts \
                translations/mx-cleanup_eu.ts \
                translations/mx-cleanup_fa.ts \
                translations/mx-cleanup_fi_FI.ts \
                translations/mx-cleanup_fil_PH.ts \
                translations/mx-cleanup_fil.ts \
                translations/mx-cleanup_fi.ts \
                translations/mx-cleanup_fr_BE.ts \
                translations/mx-cleanup_fr.ts \
                translations/mx-cleanup_fy.ts \
                translations/mx-cleanup_ga.ts \
                translations/mx-cleanup_gd.ts \
                translations/mx-cleanup_gl_ES.ts \
                translations/mx-cleanup_gl.ts \
                translations/mx-cleanup_gu.ts \
                translations/mx-cleanup_ha.ts \
                translations/mx-cleanup_haw.ts \
                translations/mx-cleanup_he_IL.ts \
                translations/mx-cleanup_he.ts \
                translations/mx-cleanup_hi.ts \
                translations/mx-cleanup_hr.ts \
                translations/mx-cleanup_ht.ts \
                translations/mx-cleanup_hu.ts \
                translations/mx-cleanup_hy_AM.ts \
                translations/mx-cleanup_hye.ts \
                translations/mx-cleanup_hy.ts \
                translations/mx-cleanup_id.ts \
                translations/mx-cleanup_ie.ts \
                translations/mx-cleanup_is.ts \
                translations/mx-cleanup_it.ts \
                translations/mx-cleanup_ja.ts \
                translations/mx-cleanup_jv.ts \
                translations/mx-cleanup_kab.ts \
                translations/mx-cleanup_ka.ts \
                translations/mx-cleanup_kk.ts \
                translations/mx-cleanup_km.ts \
                translations/mx-cleanup_kn.ts \
                translations/mx-cleanup_ko.ts \
                translations/mx-cleanup_ku.ts \
                translations/mx-cleanup_ky.ts \
                translations/mx-cleanup_lb.ts \
                translations/mx-cleanup_lo.ts \
                translations/mx-cleanup_lt.ts \
                translations/mx-cleanup_lv.ts \
                translations/mx-cleanup_mg.ts \
                translations/mx-cleanup_mi.ts \
                translations/mx-cleanup_mk.ts \
                translations/mx-cleanup_ml.ts \
                translations/mx-cleanup_mn.ts \
                translations/mx-cleanup_mr.ts \
                translations/mx-cleanup_ms.ts \
                translations/mx-cleanup_mt.ts \
                translations/mx-cleanup_my.ts \
                translations/mx-cleanup_nb_NO.ts \
                translations/mx-cleanup_nb.ts \
                translations/mx-cleanup_ne.ts \
                translations/mx-cleanup_nl_BE.ts \
                translations/mx-cleanup_nl.ts \
                translations/mx-cleanup_nn.ts \
                translations/mx-cleanup_ny.ts \
                translations/mx-cleanup_oc.ts \
                translations/mx-cleanup_or.ts \
                translations/mx-cleanup_pa.ts \
                translations/mx-cleanup_pl.ts \
                translations/mx-cleanup_ps.ts \
                translations/mx-cleanup_pt_BR.ts \
                translations/mx-cleanup_pt.ts \
                translations/mx-cleanup_ro.ts \
                translations/mx-cleanup_rue.ts \
                translations/mx-cleanup_ru_RU.ts \
                translations/mx-cleanup_ru.ts \
                translations/mx-cleanup_rw.ts \
                translations/mx-cleanup_sd.ts \
                translations/mx-cleanup_si.ts \
                translations/mx-cleanup_sk.ts \
                translations/mx-cleanup_sl.ts \
                translations/mx-cleanup_sm.ts \
                translations/mx-cleanup_sn.ts \
                translations/mx-cleanup_so.ts \
                translations/mx-cleanup_sq.ts \
                translations/mx-cleanup_sr.ts \
                translations/mx-cleanup_st.ts \
                translations/mx-cleanup_su.ts \
                translations/mx-cleanup_sv.ts \
                translations/mx-cleanup_sw.ts \
                translations/mx-cleanup_ta.ts \
                translations/mx-cleanup_te.ts \
                translations/mx-cleanup_tg.ts \
                translations/mx-cleanup_th.ts \
                translations/mx-cleanup_tk.ts \
                translations/mx-cleanup_tr.ts \
                translations/mx-cleanup_tt.ts \
                translations/mx-cleanup_ug.ts \
                translations/mx-cleanup_uk.ts \
                translations/mx-cleanup_ur.ts \
                translations/mx-cleanup_uz.ts \
                translations/mx-cleanup_vi.ts \
                translations/mx-cleanup_xh.ts \
                translations/mx-cleanup_yi.ts \
                translations/mx-cleanup_yo.ts \
                translations/mx-cleanup_yue_CN.ts \
                translations/mx-cleanup_zh_CN.ts \
                translations/mx-cleanup_zh_HK.ts \
                translations/mx-cleanup_zh_TW.ts

RESOURCES += \
    images.qrc
