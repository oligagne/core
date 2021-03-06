/**
 * @file    adaptors.cpp
 * @author  Wintermute Developers <wintermute-devel@lists.launchpad.net>
 *
 *
 *
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Wintermute Linguistics is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wintermute Linguistics; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "core.hpp"
#include "plugins.hpp"
#include "adaptors.hpp"
#include "ipc.hpp"
#include <QTimer>
#include <QDBusConnection>
#include <QtDebug>

namespace Wintermute {
    namespace IPC {
        GenericAdaptor::GenericAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent), m_core(true) {
            if (IPC::System::module () != "master"){
                m_tmr = new QTimer(this);
                connect(m_tmr,SIGNAL(timeout()),this,SLOT(detect()));
                m_tmr->setInterval (1000);
                m_tmr->start ();
            }
        }

        /// @todo Find a means to send back an interface name instead of just the running module.
        void GenericAdaptor::detect() const {
            m_tmr->stop ();
            const bool l_prv = m_core;
            QDBusMessage l_ping = QDBusMessage::createMethodCall ("org.thesii.Wintermute","/Master","org.thesii.Wintermute.Master","ping");
            l_ping << IPC::System::module ();
            l_ping.setAutoStartService (true);
            QDBusMessage l_pingReply = IPC::System::bus ()->call (l_ping,QDBus::BlockWithGui);
            m_core = l_pingReply.type () != QDBusMessage::ErrorMessage;

            if (m_core != l_prv){
                if (m_core){
                    qDebug() << "(core) [D-Bus] Core module found.";
                    emit coreModuleLoaded ();
                }
                else {
                    qDebug() << "(core) [D-Bus] Core module lost.";
                    emit coreModuleUnloaded ();
                }
            }

            if (l_pingReply.type () == QDBusMessage::ErrorMessage){
                qDebug() << "(core) [D-Bus] Pong from core module:" << l_pingReply.errorMessage ();
                if (!Core::arguments ()->value ("daemon").toBool ())
                    CoreAdaptor::haltSystem ();
            }

            m_tmr->start ();
        }

        const int GenericAdaptor::pid () const { return QApplication::applicationPid (); }

        const QString GenericAdaptor::module () const { return IPC::System::module (); }
    }

    namespace Plugins {
        PluginFactoryAdaptor::PluginFactoryAdaptor() : Adaptor(Factory::instance ()) {
            setAutoRelaySignals (true);
        }

        void PluginFactoryAdaptor::loadPlugin (const QString &p_plgnName, const QDBusMessage& p_msg){
            Factory::instance ()->loadPlugin (p_plgnName);
        }

        void PluginFactoryAdaptor::unloadPlugin (const QString &p_plgnName, const QDBusMessage& p_msg){
            Factory::instance ()->unloadPlugin (p_plgnName);
        }

        const QStringList PluginFactoryAdaptor::allPlugins (const QDBusMessage& p_msg) const {
            return Factory::instance ()->allPlugins ();
        }

        const QStringList PluginFactoryAdaptor::loadedPlugins (const QDBusMessage& p_msg) const {
            return Factory::instance ()->loadedPlugins ();
        }

        /// @todo Obtain information from plug-in.
        const QSettings* PluginFactoryAdaptor::pluginInfo (const QString &p_plgnName, const QDBusMessage& p_msg) const {
            return NULL;
        }

        void PluginFactoryAdaptor::quit (const QDBusMessage& p_msg) const {
            emit aboutToQuit ();
            Factory::Shutdown ();
        }

        InstanceAdaptor::InstanceAdaptor(AbstractPlugin *p_plgn) : Adaptor(Core::instance ()) {
            if (p_plgn == NULL){
                emit pluginCantLoad (Core::arguments ()->value ("plugin").toString ());
                QApplication::quit ();
            } else {
                connect(QApplication::instance (),SIGNAL(aboutToQuit()),this,SIGNAL(aboutToQuit()));
                QDBusConnection::sessionBus ().connect ("org.thesii.Wintermute","/Factory",
                                                        "org.thesii.Wintermute.Factory","pluginCantLoad",
                                                        this,SIGNAL(pluginCantLoad(QString)));
                QDBusConnection::sessionBus ().connect ("org.thesii.Wintermute","/Factory",
                                                        "org.thesii.Wintermute.Factory","pluginLoaded",
                                                        this,SIGNAL(pluginLoaded(QString)));
                QDBusConnection::sessionBus ().connect ("org.thesii.Wintermute","/Factory",
                                                        "org.thesii.Wintermute.Factory","pluginUnloaded",
                                                        this,SIGNAL(pluginUnloaded(QString)));

                QDBusConnection::sessionBus ().connect ("org.thesii.Wintermute","/Factory",
                                                        "org.thesii.Wintermute.Factory","aboutToQuit",
                                                        this,SLOT(aboutToQuit()));

                setParent(p_plgn);
                //setAutoRelaySignals (true);
            }
        }

        void InstanceAdaptor::quit (const QDBusMessage& p_msg) const {
            const QString l_uuid = qobject_cast<AbstractPlugin*>(parent())->uuid();
            emit aboutToQuit ();
            qobject_cast<AbstractPlugin*>(parent())->doDeinitialize();
            emit pluginUnloaded (l_uuid);
        }
    }

    CoreAdaptor::CoreAdaptor() : Adaptor(Core::instance ()) { }

    const QVariantMap CoreAdaptor::arguments () const { return *(Core::arguments ()); }

    void CoreAdaptor::ping (const QString &p_src) {
        //qDebug() << "(core) [D-Bus] Ping from object" << p_src << "received.";
    }

    void CoreAdaptor::quit (const QDBusMessage& p_msg) const {
        emit aboutToQuit ();
        QDBusMessage l_msg = QDBusMessage::createMethodCall ("org.thesii.Wintermute","/Factory", "org.thesii.Wintermute.Factory","quit");
        QDBusConnection::sessionBus ().call (l_msg,QDBus::NoBlock);
        haltSystem ();
    }

    void CoreAdaptor::haltSystem () {
        if (Core::arguments ()->value ("ipc").toString () != "master"){
            QDBusMessage l_call = QDBusMessage::createMethodCall ("org.thesii.Wintermute","/Master","org.thesii.Wintermute.Master","haltSystem");
            QDBusConnection::sessionBus ().send (l_call);
        }

        QApplication::quit ();
    }
}
