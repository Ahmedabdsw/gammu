Gammu All Mobile Management Utilities - Instalace
=================================================


Binárky - Linux
===============

Mnoho distribucí již obsahuje Gammu, takže pokud můžete použít tuto verzi,
bude to pro vás nejjednodušší. Binární balíčky posledních verzí pro mnoho
distribucí naleznete také na stránkách Gammu - <http://cs.wammu.eu/gammu/>.


Binárky - Windows
=================

Binárky pro Windows si můžete stáhnout z <http://cs.wammu.eu/gammu/>. Pro
Windows 95, 98 a NT 4.0 budete také potřebovat ShFolder DLL, která může být
stažena od Microsoftu:

http://www.microsoft.com/downloads/details.aspx?displaylang=en&FamilyID=6AE02498-07E9-48F1-A5D6-DBFA18D37E0F


Ze zdrojového kódu - požadavky
==============================

Gammu je možné zkompilovat bez jakýchkoliv dalších knihoven, ale můžete
postrádat některé funkce. Volitelné závislosti jsou:

Bluez-libs 
    - http://www.bluez.org/
    - Podpora Bluetooth na Linuxu.

libusb-1.0
    - http://libusb.sourceforge.net/
    - připojení fbususb

libCURL
    - http://curl.haxx.se/libcurl/
    - Notifikace o nových verzích a přístup do databáze telefonů.

libiconv
    - http://www.gnu.org/software/libiconv/
    - Podpora pro víc kódování v AT subsystému.

Gettext
    - http://www.gnu.org/software/gettext/
    - Překlady textů.

MySQL
    - http://mysql.com/
    - podpora MySQL v SMSD.

PostgreSQL
    - http://www.postgresql.org/
    - podpora PostgreSQL v SMSD.

libdbi
    - http://libdbi.sourceforge.net/
    - je nutná alespoň verze 0.8.2
    - podpora DBI v SMSD.
        - pro testování prosím nainstalujte libdbd-sqlite3

Python
    - http://www.python.org/
    - Gammu poskytuje rozhraní v Pythonu

SQLite + libdbi-drivers se SQLite
    - http://www.sqlite.org/
    - potřeba pro testování SMSD s ovladačem libdbi


Ze zdrojového kódu - Linux
==========================

Pro kompilaci Gammu potřebujete CMake ze stránek <http://www.cmake.org>.

Kvůli kompatibilitě Gammu obsahuje jednoduchý wrapper který se chová jako
configure skript, takže můžete použít obvyklou sadu příkazů "./configure;
make; sudo make install". Kompilace probíhá v adresáži build-configure (ve
stromu se zdrojovými kódy se nic nemění), například program gammu je
kompilován v adresáři build-configure/gammu.

Pokud chcete nebo potřebujete nastavit kompilaci víc než umožňuje wrapper
pro configure, musíte použít přímo CMake. V současné době je podporována jen
kompilace mimo zdrojový strom, takže musíte vytvořit samostatný adresář pro
kompilaci:

> mkdir build > cd build

Poté zkonfigurujte projekt:

> cmake ..

Zkompilujte ho:

> make

Otestujte jestli je všechno v pořádku:

> make test

A nakonec ho nainstalujte:

> sudo make install

Parametry můžete měnit na příkazové řádce (jak je popsáno níže), nebo pomocí
textového rozhraní ccmake.

Užitečné parametry cmake:

* -DBUILD_SHARED_LIBS=ON povolí sdílenou knihovnu
* -DCMAKE_BUILD_TYPE="Debug" zapne ladicí build
* -DCMAKE_INSTALL_PREFIX="/usr" změní prefix pro instalaci
* -DENABLE_PROTECTION=OFF vypne různé ochrany proti přetečení bufferu a
  podobným útokům
* -DBUILD_PYTHON=/usr/bin/python2.6 změní Python, který bude použit pro
  kompilaci modulu pro Python
* -DWITH_PYTHON=OFF vypne kompilaci python-gammu

Můžete také vypnout podporu pro různé druhy telefonů, např.:

* -DWITH_NOKIA_SUPPORT=OFF vypne podporu pro Nokie
* -DWITH_BLUETOOTH=OFF vypne podporu pro Bluetooth
* -DWITH_IRDA=OFF vypne podporu pro IrDA

Omezení nainstalovaných věcí
============================

Nastavením následujících parametrů můžete ovlivnit které volitelné části
budou nainstalovány:

* INSTALL_GNAPPLET - instalovat binárky Gnappletu
* INSTALL_MEDIA - instalovat ukázkové soubory s logy a vyzváněními
* INSTALL_PHP_EXAMPLES - instalovat ukázkové skripty v PHP
* INSTALL_BASH_COMPLETION - instalovat skript na doplňování parametrů Gammu
  v bashi
* INSTALL_LSB_INIT - instalovat LSB kompatibilní init skript pro Gammu
* INSTALL_DOC - instalovat dokumentaci
* INSTALL_LOC - instalovat data překladu aplikace

Například:

-DINSTALL_DOC=OFF


Ze zdrojového kódu - Windows
============================

Pro vytvoření projektu pro kompilaci Gammu bude potřebovat CMake z
<http://www.cmake.org>. CMake umí vytvořit projekty pro většinu běžně
používaných IDE včetně Microsoft Visual Studio, nástroje Borland, Cygwin
nebo Mingw32. Stačí kliknout na CMakeLists.txt ve zdrojových kódech a
nastavit CMake tak, aby byl schopen nalézt volitelné knihovny (v sekci o
křížové kompilaci naleznete informace jak je získat). Výsledkem by měl být
projekt, se kterým budete moci pracovat stejně jako s jakýmkoliv jiným ve
vašem IDE.

Podrobnější informace naleznete na wiki:
http://www.gammu.org/wiki/index.php?title=Gammu:Compiling/installing_in_Windows

Borland toolchain - you can download compiler at
<http://www.codegear.com/downloads/free/cppbuilder>. You need to add
c:/Borland/BCC55/Bin to system path (or manually set it when running CMake)
and add -Lc:/Borland/BCC55/Lib -Ic:/Borland/BCC55/Include
-Lc:/Borland/BCC55/Lib/PSDK to CMAKE_C_FLAGS in CMake (otherwise compilation
fails).

Ze zdrojového kódu - Mac OS X
=============================

Gammu by mělo jít zkompilovat na Mac OS X, aktuální informace by měly být na
wiki:

http://www.gammu.org/wiki/index.php?title=Gammu:Compiling/installing_on_Mac_OS_X


Křížová kompilace pro Windows na Linuxu
=======================================

Only cross compilation using CMake has been tested. You need to install
MinGW cross tool chain and run time. On Debian you can do it by apt-get
install mingw32. Build is then quite simple:

mkdir build-win32 cd build-win32 cmake
.. -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-mingw32.cmake make

Pokud nebyl váš kompilátor pro MinGW nalezen automaticky, můžete zadat jeho
jméno v cmake/Toolchain-mingw32.cmake.

Pro kompilaci statické knihovny bez jakýhkoliv externích závislostí spusťte:

cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-mingw32.cmake \
    -DBUILD_SHARED_LIBS=OFF \
    -DWITH_MySQL=OFF \
    -DWITH_Postgres=OFF \
    -DWITH_GettextLibs=OFF \
    -DWITH_Iconv=OFF \
    -DWITH_CURL=OFF \

To be compatible with current Python on Windows, we need to build against
matching Microsoft C Runtime library. For Python 2.4 and 2.5 MSVCR71 was
used, for Python 2.6 the right one is MSVCR90. To achieve building against
different MSVCRT, you need to adjust compiler specifications, example is
shown in cmake/mingw.spec, which is used by CMakeLists.txt. You might need
to tune it for your environment.

Externí knihovny
----------------

Nejsnazší způsob zadání cest k používaným knihovnám je v souboru
cmake/Toolchain-mingw32.cmake nebo jejich zadáním v parametru
CMAKE_FIND_ROOT_PATH při spuštění cmake.


MySQL
-----

Binární balíčky MySQL můžete stáhnout z <http://dev.mysql.com/>, po
rozbalení je potřeba provést pár úprav:

cd mysql/lib/opt
reimp.exe -d libmysql.lib
i586-mingw32msvc-dlltool --kill-at --input-def libmysql.def \
    --dllname libmysql.dll --output-lib libmysql.a

reimp.exe is part of mingw-utils and can be run through wine, I didn't try
to compile native binary from it.


PostgreSQL
----------

Můžete si stáhnout binární balíčky PostgreSQL z
<http://www.postgresql.org/>, ale budete muset odjinud přidat knihovnu
wldap32.dll do adresáře bin.


Gettext
-------

Pro podporu překladu programu potřebujete gettext-0.14.4-bin.zip,
gettext-0.14.4-dep.zip, gettext-0.14.4-lib.zip z
<http://gnuwin32.sourceforge.net/>. Rozbalte je všechny do jednoho adresáře.


CURL
----

Pro podporu CURL potřebujete curl-7.19.0-devel-mingw32.zip z
<http://curl.haxx.se/>.



# vim: et ts=4 sw=4 sts=4 tw=72 spell spelllang=en_us
