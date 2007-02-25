=========================================================================
    PROCÉDURE STOCKÉE ÉTENDUE : vue d'ensemble du projet WDMDrv
========================================================================


AppWizard a créé WDMDrv.dll pour vous.  

Ce fichier contient un résumé du contenu de chacun des fichiers qui
constituent votre application WDMDrv.

WDMDrv.vcproj
    Il s'agit du fichier projet principal pour les projets VC++ générés à l'aide d'un Assistant Application. 
    Il contient les informations sur la version de Visual C++ qui a généré le fichier et 
    des informations sur les plates-formes, configurations et fonctionnalités du projet sélectionnées avec
    l'Assistant Application.

WDMDrv.cpp
    Il s'agit du fichier source principal de la DLL.

proc.cpp
    Ce fichier contient la procédure stockée xp_proc

/////////////////////////////////////////////////////////////////////////////
Autres fichiers standard :

StdAfx.h, StdAfx.cpp
    Ces fichiers sont utilisés pour générer un fichier d'en-tête précompilé (PCH) 
    nommé WDMDrv.pch et un fichier de type précompilé nommé StdAfx.obj.


/////////////////////////////////////////////////////////////////////////////
Autres remarques :

Après avoir terminé cet Assistant, copiez WDMDrv.dll dans votre répertoire 
\Binn SQL Server.

Ajoutez votre nouvelle procédure stockée étendue à partir d'un projet de données Visual Studio 
ou en utilisant SQL Server Enterprise Manager ou en exécutant la commande 
SQL suivante :
  sp_addextendedproc 'xp_proc', 'WDMDrv.DLL'

Vous pouvez supprimer la procédure stockée étendue en utilisant la commande SQL :
  sp_dropextendedproc 'xp_proc'

Vous pouvez libérer la DLL du serveur (pour supprimer ou remplacer le fichier), en 
utilisant la commande SQL :
  DBCC xp_proc(FREE)


/////////////////////////////////////////////////////////////////////////////
