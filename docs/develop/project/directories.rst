Directory structure
===================

libgammu directory
------------------

This directory contains sources of Gammu library. You can find all phone
communication and data encoding functionality here.

There are following subdirectories:

``device``
  drivers for devices such serial ports or irda
``device/serial``
  drivers for serial ports
``device/irda``
  drivers for infrared over sockets
``protocol``
  protocol drivers
``protocol/nokia``
  Nokia specific protocols
``phone``
  phone modules
``phone/nokia``
  modules for different Nokia phones
``misc``
  different services. They can be used for any project
``service``
  different gsm services for logos, ringtones, etc.

gammu directory
---------------

Sources of Gammu command line utility. It contains interface to libGammu
and some additional functionality as well.

smsd directory
--------------

Sources of SMS Daemon as well as all it's service backends.

The ``services`` subdirectory contains source code for :ref:`smsd_services`.

python directory
----------------

Sources of python-gammu module and some examples.

helper directory
----------------

These are some helper functions used either as replacement for
functionality missing on some platforms (eg. strptime) or used in more
places (message command line processing which is shared between SMSD and
Gammu utility).

docs directory
--------------

Documentation for both end users and developers as well as SQL scripts
for creating SMSD database.

admin directory
---------------

Administrative scripts for updating locales, making release etc.

cmake directory
---------------

CMake include files and templates for generated files.

include directory
-----------------

Public headers for libGammu.

locale directory
----------------

Gettext po files for translating Gammu, libGammu and user documentation.
See :doc:`localization` for more information.

tests directory
---------------

CTest based test suite for libGammu.
See :doc:`testing` for more information.

utils directory
---------------

Various utilities usable with Gammu.

contrib directory
-----------------

Third party contributions based on Gammu.

