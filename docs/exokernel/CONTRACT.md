# XASMOS exokernel contract (v0)

## Principio
El kernel multiplexa recursos físicos y verifica capacidades. No nombra
recursos, no decide quién recibe qué, no sabe de formatos de disco ni de
programas concretos. Todo lo que dice QUIÉN accede a QUÉ vive en espacio de usuario.

## Mecanismos del kernel (y sólo estos)
1. Frames físicos: asignación y liberación desde un pool.
2. Capacidades (tokens): crear, derivar (subrango y subderechos), entregar
   (requiere GIVE), revocar (árbol), liberar (requiere RELEASE).
3. Espacios de direcciones: mapear y desmapear un frame por una capacidad MAP.
4. Ejecución: lanzar un contexto con registros iniciales, ceder, terminar.
5. Interrupciones y temporizador: entrega a quien tiene la capacidad del vector.
6. Mediación de DMA: sólo mientras no haya IOMMU. Valida descriptores con
   pines; no decide nada más.
7. Raíces: acuñar una capacidad sobre un recurso físico (un dispositivo, un
   rango de frames). Acuñar no concede acceso a nadie.

## Prohibido en el kernel
- Nombres de recursos o de dispositivos (rd0, nd0, sd0, |dev|, rutas).
- Tablas de concesiones o de políticas por dueño o por programa.
- Cuotas por dueño (g_quota). El presupuesto de memoria es una capacidad.
- Sistemas de ficheros, formatos de volumen, shell, install, format.
- Decidir destinatarios de capacidades o dueños fijados.

## Presupuesto de memoria
Una capacidad de frames (un "pool") que el dueño posee. Asignar descuenta de
ella; liberar devuelve al mismo pool. Derivar subpools es atenuación.
El kernel no conoce límites por dueño: sólo verifica que el pool presentado
tiene frames y que el token es suyo.

## Revocación
Cada entrada de capacidad guarda su padre. Revocar una capacidad retira
también todas sus descendientes. Recorrido acotado por el tamaño de la tabla.
Un hijo sin RELEASE se recupera revocando a su padre.

## Donde vive lo que quedó fuera
- Montaje y lectura de volúmenes: programa de sistema de ficheros (.exb).
- Shell y utilidades: .exb.
- Reparto inicial de raíces: programa de arranque (.exb), no el kernel.
- Consola: capacidad de dispositivo delegada, no llamada del kernel.

## Deudas declaradas
- Mediación de DMA sin IOMMU (hasta AM5 con IOMMU).
- Consola de diagnóstico del arranque en serie, hasta que exista la capacidad.
