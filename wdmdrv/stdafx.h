// stdafx.h : fichier Include pour les fichiers Include système standard,
// ou les fichiers Include spécifiques aux projets qui sont utilisés fréquemment,
// et sont rarement modifiés
//

#pragma once


// Insérez ici vos en-têtes
#define WIN32_LEAN_AND_MEAN		// Exclure les en-têtes Windows rarement utilisés

#include <windows.h>
#include <stdio.h>

//Inclure les en-têtes ODS
#ifdef __cplusplus
extern "C" {
#endif 

#include <Srv.h>		// Fichier d'en-tête principal qui contient tous les autres fichiers d'en-tête

#ifdef __cplusplus
}
#endif 

