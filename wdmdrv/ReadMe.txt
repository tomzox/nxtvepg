=========================================================================
    PROC�DURE STOCK�E �TENDUE : vue d'ensemble du projet WDMDrv
========================================================================


AppWizard a cr�� WDMDrv.dll pour vous.  

Ce fichier contient un r�sum� du contenu de chacun des fichiers qui
constituent votre application WDMDrv.

WDMDrv.vcproj
    Il s'agit du fichier projet principal pour les projets VC++ g�n�r�s � l'aide d'un Assistant Application. 
    Il contient les informations sur la version de Visual C++ qui a g�n�r� le fichier et 
    des informations sur les plates-formes, configurations et fonctionnalit�s du projet s�lectionn�es avec
    l'Assistant Application.

WDMDrv.cpp
    Il s'agit du fichier source principal de la DLL.

proc.cpp
    Ce fichier contient la proc�dure stock�e xp_proc

/////////////////////////////////////////////////////////////////////////////
Autres fichiers standard�:

StdAfx.h, StdAfx.cpp
    Ces fichiers sont utilis�s pour g�n�rer un fichier d'en-t�te pr�compil� (PCH) 
    nomm� WDMDrv.pch et un fichier de type pr�compil� nomm� StdAfx.obj.


/////////////////////////////////////////////////////////////////////////////
Autres remarques�:

Apr�s avoir termin� cet Assistant, copiez WDMDrv.dll dans votre r�pertoire 
\Binn SQL Server.

Ajoutez votre nouvelle proc�dure stock�e �tendue � partir d'un projet de donn�es Visual Studio 
ou en utilisant SQL Server Enterprise Manager ou en ex�cutant la commande 
SQL suivante�:
  sp_addextendedproc 'xp_proc', 'WDMDrv.DLL'

Vous pouvez supprimer la proc�dure stock�e �tendue en utilisant la commande SQL�:
  sp_dropextendedproc 'xp_proc'

Vous pouvez lib�rer la DLL du serveur (pour supprimer ou remplacer le fichier), en 
utilisant la commande SQL�:
  DBCC xp_proc(FREE)


/////////////////////////////////////////////////////////////////////////////
