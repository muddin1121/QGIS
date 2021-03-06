/***************************************************************************
                          qgsserviceregistry.cpp

  Class defining the service manager for QGIS server services.
  -------------------
  begin                : 2016-12-05
  copyright            : (C) 2016 by David Marteau
  email                : david dot marteau at 3liz dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsserviceregistry.h"
#include "qgsservice.h"
#include "qgsmessagelog.h"

#include <algorithm>
#include <functional>


namespace
{

// Build a key entry from name and version
  QString makeServiceKey( const QString &name, const QString &version )
  {
    return QString( "%1_%2" ).arg( name, version );
  }

// Compare two version strings:
// The strings are split into dot separated segment
// Each segment are compared up to the shortest number of segment of the
// lists. Remaining segments are dropped.
// If both segments can be interpreted as numbers the are compared as numbers, otherwise
// They are compared lexicographically.
// Return true if v1 is greater than v2
  bool isVersionGreater( const QString &v1, const QString &v2 )
  {
    QStringList l1 = v1.split( '.' );
    QStringList l2 = v2.split( '.' );
    QStringList::iterator it1 = l1.begin();
    QStringList::iterator it2 = l2.begin();
    bool isint;
    while ( it1 != l1.end() && it2 != l2.end() )
    {
      if ( *it1 != *it2 )
      {
        // Compare as numbers
        int i1 = it1->toInt( &isint );
        if ( isint )
        {
          int i2 = it2->toInt( &isint );
          if ( isint && i1 != i2 )
          {
            return i1 > i2;
          }
        }
        // Compare lexicographically
        if ( !isint )
        {
          return *it1 > *it2;
        }
      }
      ++it1;
      ++it2;
    }
    // We reach the end of one of the list
    return false;
  }

// Check that two versions are c


} // namespace

QgsServiceRegistry::~QgsServiceRegistry()
{
  cleanUp();
}

QgsService *QgsServiceRegistry::getService( const QString &name, const QString &version )
{
  QgsService *service = nullptr;
  QString key;

  // Check that we have a service of that name
  VersionTable::const_iterator v = mVersions.constFind( name );
  if ( v != mVersions.constEnd() )
  {
    key = version.isEmpty() ? v->second : makeServiceKey( name, version );
    ServiceTable::const_iterator it = mServices.constFind( key );
    if ( it != mServices.constEnd() )
    {
      service = it->get();
    }
    else
    {
      // Return the dofault version
      QgsMessageLog::logMessage( QString( "Service %1 %2 not found, returning default" ).arg( name, version ) );
      service = mServices[v->second].get();
    }
  }
  else
  {
    QgsMessageLog::logMessage( QString( "Service %1 is not registered" ).arg( name ) );
  }
  return service;
}

void QgsServiceRegistry::registerService( QgsService *service )
{
  QString name    = service->name();
  QString version = service->version();

  // Test if service is already registered
  QString key = makeServiceKey( name, version );
  if ( mServices.constFind( key ) != mServices.constEnd() )
  {
    QgsMessageLog::logMessage( QString( "Error Service %1 %2 is already registered" ).arg( name, version ) );
    return;
  }

  QgsMessageLog::logMessage( QString( "Adding service %1 %2" ).arg( name, version ) );
  mServices.insert( key, std::shared_ptr<QgsService>( service ) );

  // Check the default version
  // The first inserted service of a given name
  // is the default one.
  // this will ensure that native services are always
  // the defaults.
  VersionTable::const_iterator v = mVersions.constFind( name );
  if ( v == mVersions.constEnd() )
  {
    // Insert the service as the default one
    mVersions.insert( name, VersionTable::mapped_type( version, key ) );
  }
  /*
  if ( v != mVersions.constEnd() )
  {
    if ( isVersionGreater( version, v->first ) )
    {
      // Replace the default version key
      mVersions.insert( name, VersionTable::mapped_type( version, key ) );
    }
  }
  else
  {
    // Insert the service as the default one
    mVersions.insert( name, VersionTable::mapped_type( version, key ) );
  }*/

}

int QgsServiceRegistry::unregisterService( const QString &name, const QString &version )
{
  // Check that we have a service of that name
  int removed = 0;
  VersionTable::const_iterator v = mVersions.constFind( name );
  if ( v != mVersions.constEnd() )
  {
    if ( version.isEmpty() )
    {
      // No version specified, remove all versions
      ServiceTable::iterator it = mServices.begin();
      while ( it != mServices.end() )
      {
        if ( ( *it )->name() == name )
        {
          QgsMessageLog::logMessage( QString( "Unregistering service %1 %2" ).arg( name, ( *it )->version() ) );
          it = mServices.erase( it );
          ++removed;
        }
        else
        {
          ++it;
        }
      }
      // Remove from version table
      mVersions.remove( name );
    }
    else
    {
      QString key = makeServiceKey( name, version );
      ServiceTable::iterator found = mServices.find( key );
      if ( found != mServices.end() )
      {
        QgsMessageLog::logMessage( QString( "Unregistering service %1 %2" ).arg( name, version ) );
        mServices.erase( found );
        removed = 1;

        // Find if we have other services of that name
        // but with different version
        //
        QString maxVer;
        std::function < void ( const ServiceTable::mapped_type & ) >
        findGreaterVersion = [name, &maxVer]( const ServiceTable::mapped_type & service )
        {
          if ( service->name() == name &&
               ( maxVer.isEmpty() || isVersionGreater( service->version(), maxVer ) ) )
            maxVer = service->version();
        };

        mVersions.remove( name );

        std::for_each( mServices.constBegin(), mServices.constEnd(), findGreaterVersion );
        if ( !maxVer.isEmpty() )
        {
          // Set the new default service
          QString key = makeServiceKey( name, maxVer );
          mVersions.insert( name, VersionTable::mapped_type( version, key ) );
        }
      }
    }
  }
  return removed;
}

void QgsServiceRegistry::init( const QString &nativeModulePath, QgsServerInterface *serverIface )
{
  mNativeLoader.loadModules( nativeModulePath, *this, serverIface );
}

void QgsServiceRegistry::cleanUp()
{
  // Release all services
  mVersions.clear();
  mServices.clear();
  mNativeLoader.unloadModules();
}


