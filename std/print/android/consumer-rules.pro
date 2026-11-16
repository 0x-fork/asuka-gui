# AsukaPdfWriter subclasses the print callbacks in their package
# (android.print), which R8 mustn't rename.
-keep class android.print.AsukaPdfWriter** { *; }
