vmware_view_CPPFLAGS += -I$(srcdir)/cvp

nodist_vmware_view_SOURCES += cvp/cvpApp.cc
nodist_vmware_view_SOURCES += cvp/cvpApp.hh
nodist_vmware_view_SOURCES += cvp/cvpChangeServerDlg.cc
nodist_vmware_view_SOURCES += cvp/cvpChangeServerDlg.hh
nodist_vmware_view_SOURCES += cvp/cvpHelpSupportDlg.cc
nodist_vmware_view_SOURCES += cvp/cvpHelpSupportDlg.hh
nodist_vmware_view_SOURCES += cvp/cvpWindow.cc
nodist_vmware_view_SOURCES += cvp/cvpWindow.hh
nodist_vmware_view_SOURCES += cvp/cvpa.cc
nodist_vmware_view_SOURCES += cvp/cvpa.hh
nodist_vmware_view_SOURCES += cvp/cvpaXml.cc
nodist_vmware_view_SOURCES += cvp/cvpaXml.hh
nodist_vmware_view_SOURCES += cvp/downloadDlg.cc
nodist_vmware_view_SOURCES += cvp/downloadDlg.hh
nodist_vmware_view_SOURCES += cvp/expiredDlg.cc
nodist_vmware_view_SOURCES += cvp/expiredDlg.hh
nodist_vmware_view_SOURCES += cvp/shutdownDlg.cc
nodist_vmware_view_SOURCES += cvp/shutdownDlg.hh
nodist_vmware_view_SOURCES += cvp/progressDlg.cc
nodist_vmware_view_SOURCES += cvp/progressDlg.hh
nodist_vmware_view_SOURCES += cvp/lockoutOverrideDlg.cc
nodist_vmware_view_SOURCES += cvp/lockoutOverrideDlg.hh
nodist_vmware_view_SOURCES += cvp/cvpResponseEntry.c
nodist_vmware_view_SOURCES += cvp/cvpResponseEntry.h
nodist_vmware_view_SOURCES += cvp/fieldEntry.c
nodist_vmware_view_SOURCES += cvp/fieldEntry.h
nodist_vmware_view_SOURCES += cvp/main.cc

dist_noinst_DATA += icons/endpoint_shutdown.png
dist_noinst_DATA += icons/endpoint_restart.png
dist_noinst_DATA += icons/endpoint_sleep.png

cvp/vmware_view-shutdownDlg.$(OBJEXT): icons/endpoint_shutdown.h
cvp/vmware_view-shutdownDlg.$(OBJEXT): icons/endpoint_restart.h
cvp/vmware_view-shutdownDlg.$(OBJEXT): icons/endpoint_sleep.h
cvp/vmware_view-cvpWindow.$(OBJEXT): icons/desktop_remote32x.h
cvp/vmware_view-cvpWindow.$(OBJEXT): icons/desktop_remote32x_disabled.h
