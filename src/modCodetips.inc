'    WinFBE - Programmer's Code Editor for the FreeBASIC Compiler
'    Copyright (C) 2016-2020 Paul Squires, PlanetSquires Software
'
'    This program is free software: you can redistribute it and/or modify
'    it under the terms of the GNU General Public License as published by
'    the Free Software Foundation, either version 3 of the License, or
'    (at your option) any later version.
'
'    This program is distributed in the hope that it will be useful,
'    but WITHOUT any WARRANTY; without even the implied warranty of
'    MERCHANTABILITY or FITNESS for A PARTICULAR PURPOSE.  See the
'    GNU General Public License for more details.

#include once "modCodetips.bi"
#include once "clsDocument.bi"


' ========================================================================================
' Format the codetip prior to showing it
' ========================================================================================
private function FormatCodetip( byval strCodeTip as string ) as STRING
   dim as long p

   ' remove multiple duplicate spaces 
   strCodeTip = AfxStrReplace(strCodeTip, "  ", " ") 

   ' If this is only a small CodeTip then don't bother trying
   ' to format it into multiple lines.
   if len(strCodeTip) < 75 THEN return strCodeTip

   IF INSTR(strCodeTip, "( ") THEN
      strCodeTip = AfxStrReplace(strCodeTip, "( ", "(  ")
   ELSEIF INSTR(strCodeTip, "(") THEN
      strCodeTip = AfxStrReplace(strCodeTip, "(", "(  ")
   END IF
   p = INSTR(strCodeTip, "(")
   IF p THEN
      IF MID(strCodeTip, p + 1, 1) <> ")" AND MID(strCodeTip, p + 1, 2) <> " )" THEN
         strCodeTip = AfxStrReplace(strCodeTip, "(", "( _" & vblf)
         IF INSTR(strCodeTip, " )") THEN
            strCodeTip = AfxStrReplace(strCodeTip, ")", "_" & vblf & ")")
         ELSEIF INSTR(strCodeTip, ")") THEN
            strCodeTip = AfxStrReplace(strCodeTip, ")", " _" & vblf & ")")
         END IF
      END IF
   END IF
   strCodeTip = AfxStrReplace(strCodeTip, ", ", ",")
   strCodeTip = AfxStrReplace(strCodeTip, " ,", ",")
   strCodeTip = AfxStrReplace(strCodeTip, ",", ", _" & vblf & "  ")
   
   FUNCTION = strCodeTip
end function


' ========================================================================================
' Determine what variable relates to the "With" statement
' ========================================================================================
private function DetermineWithVariable( byval pDoc as clsDocument ptr) as string
   dim as hwnd hEdit = pDoc->hWndActiveScintilla
   dim as long nCurLine = pDoc->GetCurrentLineNumber - 1
   dim as string sLine, sWithVariable 
   
   for i as long = nCurLine to 0 step -1
      sLine = ltrim(pDoc->GetLine(i))
      ' Remove double spaces and replace TABs with single space
      sLine = AfxStrShrink(sLine, chr(32,9))
      if left(ucase(sLine), 5) = "WITH " then
         sWithVariable = AfxStrParse(sLine, 2, " ")
         exit for
      END IF
   next
   
   function = sWithVariable
end function    



' ========================================================================================
' Take the current line and determine what variable is being referenced.
' Dereferences expressions like:  g.MyFunction, g.ot.rc, g->MyFunction
' Used for determining what popup autocomplete list or codetip to display.
' ========================================================================================
private function DereferenceLine( byval pDoc as clsDocument ptr, _
                                  byval sTrigger as String, _
                                  byval nPosition as long _
                                  ) as DB2_DATA ptr
   if pDoc = 0 THEN exit function
   
   dim pData     as DB2_DATA ptr
   dim pDataTYPE as DB2_DATA ptr
   dim pLastData as DB2_DATA ptr
   
   dim as long i, nTotalElements, nCurElement
   dim as ClassProperty nGetSet
   dim as string sElement, sVarName, sCurrentFunction, sTypeName, sLocal, sGlobal, sTemp
   dim as Boolean bIsTHIS

   dim as hwnd hEdit   = pDoc->hWndActiveScintilla
   dim as long curPos  = nPosition  
   dim as long nCol    = SciExec(hEdit, SCI_GETCOLUMN, curPos, 0) 
   dim as string sLine = trim(left(pDoc->GetLine(pDoc->GetCurrentLineNumber), nCol+1), any chr(32,9))

   ' Comment out any comment line so the popup isn't activated when in a comment. 
   sLine = MaskStringCharacters(sLine)
   sLine = RemoveComments(sLine)

   ' Test to ensure that the incoming sTrigger character still exists after
   ' the commenting of the line. If it does then we know that the line
   ' is good and we can proceed to dereference elements on the line.
   ' The identifer that triggers an autocomplete list for a TYPE variable
   ' is either a dot "." or a pointer dereference "->"
   if right(sLine, len(sTrigger)) <> sTrigger THEN exit function
   sLine = rtrim(sLine, sTrigger)


   ' Proceed to parse the line. Find the beginning of the line in order to
   ' isolate the expression to dereference. The expression might contain an
   ' array reference so that would have to be removed, however the "(" can
   ' also signify the start position of the expression. For example:
   ' st = myFunction(myvar(3).myvalue)
   ' Simply reverse searching for the "(" would fail because the array "()"
   ' also needs to be taken into account. Therefore, search in reverse and
   ' take note of when the ")" is encountered in order to then match it with
   ' an "(".
   ' Need to alo handle situations where there are multiple () array references
   ' in the expression. For example:  ListView1.Item(10).rc(5)
   dim as long nStart, nEnd
   do
      do
         nEnd = InStrRev(sLine, ")", -1)
         if nEnd = 0 then exit do
         nStart = InStrRev(sLine, "(", nEnd)
         if nStart = 0 then exit do
         ' Remove any array parenthesis.
         sLine = left(sLine, nStart-1) & mid(sLine, nEnd+1)
      LOOP
      if (nStart = 0) or (nEnd = 0) then exit do
   loop
   
   ' All of the array parenthesis should now be removed so now we can simply
   ' search for the start of the line.
   i = InStrRev(sLine, any " (*[&@", -1)
   if i THEN sLine = mid(sLine, i+1)
   
   ' Need to check if this line is part of a WITH / END WITH block.
   if (len(sLine) = 0) or (left(sLine, 1) = ".") then
      sLine = DetermineWithVariable(pDoc) & sLine
   END IF
   
   ' Make it easier to parse by converting line to uppercase
   sLine = ucase(sLine)
   
   ' Convert all "->" pointer references to "." dots to make parsing easier
   sLine = AfxStrReplace(sLine, "->", ".")
   nTotalElements = AfxStrParseCount(sLine, ".")
   nCurElement = 1
   
   ' Determine what sub/function (if any) that we are in. This function will
   ' retrieve the current standalone function, or the fully qualified name of 
   ' a class/type function.
   ' eg.   clsType.MyFunctionName      ' inside a class/type function
   ' eg.   MyFunctionName              ' standalone function
   pDoc->GetCurrentFunctionName( sCurrentFunction, nGetSet )

   do until nCurElement > nTotalElements
      sElement = AfxStrParse(sLine, nCurElement, ".")   ' p | MyFunction

      ' SEARCH FOR MATCHING VARIABLE NAME
      ' If the special keyword "THIS" is used then convert it to the function name
      if nCurElement = 1 then
         if ucase(sElement) = "THIS" then
            sElement = AfxStrParse(sCurrentFunction, 1, ".")
            bIsTHIS = true
         else
            bIsTHIS = false
         END IF
      END IF
      sVarName = sVarName & sElement

      '' ATTEMPT TO MATCH A FUNCTION NAME
      ' eg.    MyFunction
      ' eg.    mytype.MyFunction
      pData = gdb2.dbFindFunction( sVarName )
      if pData then return pData
      pData = gdb2.dbFindSub( sVarName )
      if pData then return pData
      pData = gdb2.dbFindProperty( sVarName )
      if pData then return pData
                 
      '' ATTEMPT TO MATCH LOCAL VARIABLE
      ' <FunctionName>.<Element>
      ' <TYPEname>.<FunctionName>.<Element>
      sLocal = sCurrentFunction & "." & sVarName
      if AfxStrTally(sLocal, ".") = 1 then sLocal = "." & sLocal

      pData = gdb2.dbFindVariable(sLocal)
      if pData = 0 THEN 
         '' ATTEMPT TO MATCH ON A TYPE ELEMENT
         if pLastData then
            sTemp = pLastData->VariableType & ".." & sElement
         else
            sTemp = sVarName & "." & sElement
         end if
         pData = gdb2.dbFindVariable(sTemp)
         
         '' ATTEMPT TO MATCH GLOBAL VARIABLE   
         if pData = 0 then
            sGlobal = ".." & sElement     ' Global variable format ..<ElementName>
            pData = gdb2.dbFindVariable(sGlobal)
         end if
      END IF              
          
      '' ATTEMPT TO MATCH ON A TYPE NAME
      if pData then
         ' If we are attempting to activate a CodeTip via "(" then the codetip
         ' for a TYPE function element is stored in Element Value. Return the
         ' pData now rather than continuing to then try to do a lookup for a
         ' TYPE that will fail (because ElementValue is not a TYPE in this case,
         ' but rather a codetip).
         if nCurElement = nTotalElements then return pData
         sTypeName = pData->VariableType   ' forward reference to the TYPE if applicable
      else
         sTypeName = sVarName   
      END IF

      pDataTYPE = gdb2.dbFindTYPE(sTypeName)
      if pDataTYPE then pLastData = pDataTYPE

      sVarName = sTypeName & "."
      nCurElement = nCurElement + 1
   loop
   
   if pLastData then pLastData->IsTHIS = bIsThis
   return pLastData
      
END FUNCTION

   
' ========================================================================================
' Show codetips
' ========================================================================================
private function ShowCodetip( byval pDoc as clsDocument ptr, _
                              byval bCommaTrigger as Boolean = false _
                              ) as BOOLEAN

   if gConfig.CodeTips = false then exit function
   
   dim pData as DB2_DATA ptr
   dim as string codeTip
   dim as hwnd hEdit  = pDoc->hWndActiveScintilla
   dim as long curPos = SciExec(hEdit, SCI_GETCURRENTPOS, 0, 0) 
   
   if bCommaTrigger then
      ' It is possible that a codetip had been displayed but the user moved off of the
      ' current line and thereby closed the codetip. If user goes back to the line and
      ' presses a comma to enter another parameter for function, then we should do a 
      ' test to see if we can redisplay the codetip popup again.
      ' Need to alo handle situations where there are multiple () array references
      ' in the expression. For example:  ListView1.Item(10).rc(5)
      dim as long nCol = SciExec(hEdit, SCI_GETCOLUMN, curPos, 0) 
      dim as string sLine = rtrim(left(pDoc->GetLine(pDoc->GetCurrentLineNumber), nCol+1))
      dim as boolean bInClosed
      for i as long = len(sLine) -1 to 0 step -1
         if sLine[i] = asc(")") then 
            bInClosed = true: continue for
         end if
         if sLine[i] = asc("(") then
            if bInClosed = true then
               bInClosed = false
            else
               exit for
            end if
         end if         
         curPos = curPos - 1
      next
   end if

   ' If a CodeTip already exists but was cancelled because an autocomplete
   ' popup was displayed then we simply redisplay that popup now.
   if len(pDoc->wszLastCallTip) andalso (pDoc->nLastCallTipLine = pDoc->GetCurrentLineNumber) then
      codeTip = pDoc->wszLastCallTip   
   else      
      pDoc->wszLastCallTip = ""
      pDoc->nLastCallTipLine = -1
      ' If the file is dirty then reparse it before attempting the codetip
      If cbool(SciExec( hEdit, SCI_GETMODIFY, 0, 0 )) or pDoc->UserModified Then
         pDoc->bRegenerateCode = true
         GenerateFormCode(pDoc)
         ParseDocument( pDoc )
      End If   
      pData = DereferenceLine(pDoc, "(", curPos-1)
      if pData = 0 then 
         ' Maybe this is a class sub/function inside of a class method that is
         ' not prefixed by the "this" keyword.
         dim sClassFunction as String
         pDoc->GetCurrentFunctionName( sClassFunction, 0 )
         dim as long f = instr( sClassFunction, "." )
         if f then
            ' Get the word preceeding our "("
            dim as string sFunctionName = pDoc->GetWord( curPos - 1)
            sFunctionName = left( sClassFunction, f ) & sFunctionName
            if pData = 0 then pData = gdb2.dbFindFunction( sFunctionName )
            if pData = 0 then pData = gdb2.dbFindSub( sFunctionName )
            if pData = 0 then pData = gdb2.dbFindProperty( sFunctionName )
         end if   
      end if
      if pData then codeTip = pData->CallTip
   end if

   if len(codeTip) THEN 
      pDoc->wszLastCallTip = codeTip
      pDoc->nLastCallTipLine = pDoc->GetCurrentLineNumber
      codeTip = FormatCodetip(codeTip)
      SciExec( hEdit, SCI_CALLTIPSHOW, curPos, strptr(codeTip))
      return TRUE
   else
      pDoc->AutoCompleteType = AUTOCOMPLETE_NONE
   END IF
   
   return FALSE
END FUNCTION

  
' ========================================================================================
' Display the actual Autocomplete popup list window
' ========================================================================================
private function ShowAutoCompletePopup( byval hEdit as hwnd, _
                                        byref sList as string _
                                        ) as Long
   if len(sList) = 0 THEN exit function
   dim as string sFillups = "(=."
                                  
   ' Register the PNG images       
   SciExec(hEdit, SCI_RGBAIMAGESETWIDTH, 16, 0)
   SciExec(hEdit, SCI_RGBAIMAGESETHEIGHT, 16, 0)
                                          
   SciExec(hEdit, SCI_REGISTERRGBAIMAGE, IMAGE_AUTOC_BASETYPE, gApp.pImageAutocompleteBaseType)
   SciExec(hEdit, SCI_REGISTERRGBAIMAGE, IMAGE_AUTOC_CLASS,    gApp.pImageAutocompleteClass)
   SciExec(hEdit, SCI_REGISTERRGBAIMAGE, IMAGE_AUTOC_METHOD,   gApp.pImageAutocompleteMethod)
   SciExec(hEdit, SCI_REGISTERRGBAIMAGE, IMAGE_AUTOC_PROPERTY, gApp.pImageAutocompleteProperty)
                          
   SciExec(hEdit, SCI_AUTOCSETFILLUPS, 0, strptr(sFillups))  ' characters that also select and close the popup
   SciExec(hEdit, SCI_AUTOCSETSEPARATOR, 124, 0)             ' Pipe symbol as separator
   SciExec(hEdit, SCI_AUTOCSETORDER, 1, 0)                   ' List must be sorted alphabetically
   SciExec(hEdit, SCI_AUTOCSETIGNORECASE, 1, 0)
   SciExec(hEdit, SCI_AUTOCSETMAXHEIGHT, 9, 0)
   SciExec(hEdit, SCI_AUTOCSHOW, 0, strptr(sList))
                            
   function = 0
end function

       
' ========================================================================================
' Don't add duplicates in the Autocomplete list    
' ========================================================================================
private function ExistsInAutocompleteList( byref sList as string, _
                                           byref sMatch as string _
                                           ) as boolean
   if rtrim(sMatch) = "" then return true
   dim as string sLookFor = "|" & ucase(sMatch) & "?" & "|"
   if instr( "|" & ucase(sList) & "|", sLookFor ) THEN return true
   return false
end function


' ========================================================================================
' Show Autocomplete list    
' ========================================================================================
private function ShowAutocompleteList( byval Notification as long = 0) as BOOLEAN
  
   IF gConfig.AutoComplete = false THEN exit function
   
   dim as long curPos, nCol, nLenMatchWord, ub, iNextType
   dim as string sWord, sList, st, sDot, sLookFor, sElement, sProp
   dim as Boolean bIsTHIS, bTypesOnly, bTypesNotPreloaded
   
   dim pDoc as clsDocument ptr
   dim pData as DB2_DATA ptr
   
   pDoc = gTTabCtl.GetActiveDocumentPtr()
   If pDoc = 0 Then exit function
   
   dim as hwnd hEdit = pDoc->hWndActiveScintilla
             
   ' Get the current entered word/characters to only match.
   ' Remove any trailing "." because we have modified valid scintilla to recognize
   ' the period as part of a valid word.
   pDoc->sMatchWord = ucase( trim( pDoc->GetWord, any " .") )
   nLenMatchWord = len(pDoc->sMatchWord )

   ' If an autocomplete is active but now there is no match word then
   ' a space or tab must have been pressed so autocomplete cancel and now exit.
   if SciExec(hEdit, SCI_AUTOCACTIVE, 0, 0) then
      if nLenMatchWord = 0 THEN 
         SciExec( hEdit, SCI_AUTOCCANCEL, 0, 0)
         exit function
      end if   
   END IF                 


   ' Retrieve the position
   curPos = SciExec(hEdit, SCI_GETCURRENTPOS, 0, 0) 
   nCol   = SciExec(hEdit, SCI_GETCOLUMN, curPos, 0) 
  
   ' Get the styling of the current line to determine if we are in a 
   ' multiline or single line comment block then abort the autoinsert.
   select case SciExec(hEdit, SCI_GETSTYLEAT, curPos, 0)
      case SCE_B_MULTILINECOMMENT, SCE_B_COMMENT
         exit function
   end select

   dim as string sLine = left( pDoc->GetLine(pDoc->GetCurrentLineNumber), nCol )
   dim as string sLine_ucase
   
   ' If no active autocomplete then create the list based on the current underlying match word.
   if (SciExec(hEdit, SCI_AUTOCACTIVE, 0, 0) = 0) and _
      (notification <> SCN_AUTOCCHARDELETED) and _
      (pDoc->AutoCompleteType = AUTOCOMPLETE_NONE) then
     
      ' Comment out any comment line so the popup isn't activated when in a comment.
      sLine = MaskStringCharacters(sLine)
      sLine = RemoveComments(sLine)
      pDoc->sMatchWord = ""
      nLenMatchWord = 0         
      sLine_ucase = ucase(sLine)

      if right(sLine_ucase, 4) = " AS " then 
         ' okay
         bTypesOnly = false
         bTypesNotPreloaded = false  ' all types
         pDoc->AutoCompleteType = AUTOCOMPLETE_DIM_AS

      elseif right(sLine_ucase, 1) = "." then 
         sDot = "."  ' okay
         pDoc->AutoCompleteType = AUTOCOMPLETE_TYPE

      elseif right(sLine_ucase, 2) = "->" then
         sDot = "->"   
         pDoc->AutoCompleteType = AUTOCOMPLETE_TYPE

      elseif right(sLine_ucase, 1) = "(" then 
         ShowCodetip(pDoc)
         exit function 

      elseif right(sLine_ucase, 1) = "," then 
         ShowCodetip(pDoc, true)
         exit function 

      elseif right(sLine_ucase, 5) = " NEW " then 
         ' okay
         bTypesOnly = false
         bTypesNotPreloaded = false  ' all types
         pDoc->AutoCompleteType = AUTOCOMPLETE_DIM_AS

      elseif right(sLine_ucase, 12) = "CONSTRUCTOR " then 
         ' okay
         bTypesOnly = true
         bTypesNotPreloaded = true  ' only TYPES that are in our source.
         pDoc->AutoCompleteType = AUTOCOMPLETE_DIM_AS

      elseif right(sLine_ucase, 11) = "DESTRUCTOR " then 
         ' okay
         bTypesOnly = true
         bTypesNotPreloaded = true
         pDoc->AutoCompleteType = AUTOCOMPLETE_DIM_AS

      elseif right(sLine_ucase, 9) = " EXTENDS " then 
         ' okay
         bTypesOnly = true
         bTypesNotPreloaded = false  ' all types
         pDoc->AutoCompleteType = AUTOCOMPLETE_DIM_AS
      END IF

      ' Check if it is a "FOR i AS LONG" type of statement
      if instr(sLine, "FOR ") then 
         SciExec( hEdit, SCI_AUTOCCANCEL, 0, 0)
         exit function
      END IF

      if pDoc->AutoCompleteType <> AUTOCOMPLETE_NONE then
         ' If the file is dirty then reparse it before attempting the autocomplete
         If cbool(SciExec( hEdit, SCI_GETMODIFY, 0, 0 )) or pDoc->UserModified Then
            pDoc->bRegenerateCode = true
            GenerateFormCode(pDoc)
            ParseDocument(pDoc)
         End If   
      end if
   END IF
   
   sList = "|"
         
   select case pDoc->AutoCompleteType
      
      case AUTOCOMPLETE_DIM_AS
         ' Create the space separated data type list
         ' Add all of the TYPE definitions that were found
         gdb2.dbRewind()
         do 
            pData = gdb2.dbGetNext()
            if pData = 0 THEN exit do
            
            if bTypesOnly = false then
               if pData->id = DB2_STANDARDDATATYPE then
                  st = pData->VariableType
                  if nLenMatchWord then
                     if left(ucase(st), nLenMatchWord) = pDoc->sMatchWord THEN 
                        sList = sList & st & "?" & str(IMAGE_AUTOC_BASETYPE) & "|"
                     END IF
                  else
                     if len(st) then      
                        sList = sList & st & "?" & str(IMAGE_AUTOC_BASETYPE) & "|"
                     end if   
                  END IF
               END IF      
            end if
    
            if pData->id = DB2_TYPE THEN
               if bTypesOnly then
                  if pData->IsEnum then continue do
                  if bTypesNotPreloaded then
                     ' Only look at TYPES that are loaded into the editor. These would
                     ' be entries that have a Filename attached to them. Preloaded TYPES
                     ' from WinAPI or Afx do not have Filenames.
                     if len(pData->fileName) = 0 then continue do
                  end if
               END IF
               st = rtrim(pData->VariableType)
               if nLenMatchWord then
                  if left(ucase(st), nLenMatchWord) = pDoc->sMatchWord THEN 
                     If ExistsInAutocompleteList(sList, st) = false THEN 
                        st = st & "?" & str(IMAGE_AUTOC_CLASS) 
                        sList = sList & st & "|" 
                     end if
                  end if
               elseif len(st) THEN 
                  If ExistsInAutocompleteList(sList, st) = false THEN 
                     st = st & "?" & str(IMAGE_AUTOC_CLASS) 
                     sList = sList & st & "|"
                  end if    
               END IF
            END IF
         loop


      case AUTOCOMPLETE_TYPE
         pData = DereferenceLine( pDoc, sDot, curPos - 1)
         if pData then 

            if (pData->id = DB2_VARIABLE) or (pData->id = DB2_TYPE) then
 
               bIsTHIS = pData->IsTHIS
               
               ' Construct the popup list by retrieving all elements
               ' related to the TYPE name. 
               sLookFor = pData->VariableType
   
               ' Lookup the TYPE because we need to ensure that we are using
               ' the true TYPE and not the ALIAS name. When the TYPE is found, the
               ' true TYPE can be found in pData->ElementValue.
               ' Also, this TYPE may be extended from another TYPE so we need to ensure
               ' that we gather the elements from all affected TYPEs.
               dim AllTypes(any) as string 
               
               do
                  pData = gdb2.dbFindTYPE(sLookFor)
                  if pData = 0 then exit do
                  ub = ubound(AllTypes) + 1
                  redim preserve AllTypes(ub)
                  ' the true TYPE associated with the variable
                  AllTypes(ub) = ucase(pData->VariableType)
                  sLookFor = pData->TypeExtends
               loop 
               iNextType = lbound(AllTypes)
               
               do until iNextType > ubound(AllTypes)
                  gdb2.dbRewind()
                  do 
                     pData = gdb2.dbGetNext()
                     if pData = 0 THEN exit do
                     sElement = pData->ElementName   
                     st = ucase(AfxStrParse(sElement, 1, "."))       ' MYTYPE
            
                     if AllTypes(iNextType) = st THEN   ' MYTYPE = MYTYPE
                        select case pData->id
                           case DB2_VARIABLE    ' 3 parse parts. mytype..sLine, mytype.MyFunction.rc
                              ' Only get the variable if Parse#2 is blank. This would indicate that
                              ' the variable is not part of a sub/function but rather belongs to
                              ' the TYPE structure itself.
                              st = AfxStrParse(sElement, 3, ".")   ' Get the "sLine"
                              if len(AfxStrParse(sElement, 2, ".")) = 0 then
                                 ' Only include PUBLIC elements of the TYPE unless the flag has
                                 ' been set that the THIS keyword was used.
                                 if bIsTHIS = false then
                                    if pData->IsPublic = false then continue do
                                 END IF
                                 if nLenMatchWord then
                                    if left(ucase(st), nLenMatchWord) = pDoc->sMatchWord THEN 
                                       If ExistsInAutocompleteList(sList, st) = false THEN 
                                          st = st & "?" & str(IMAGE_AUTOC_PROPERTY) 
                                          sList = sList & st & "|"
                                       end if
                                    end if
                                 else
? "["; sList; "]", "["; st; "]"
                                    If ExistsInAutocompleteList(sList, st) = false THEN 
                                       st = st & "?" & str(IMAGE_AUTOC_PROPERTY) 
                                       sList = sList & st & "|"
else
? st; " exists in "; sList                                       
                                    end if
                                 end if         
                              end if
                           
                           case DB2_FUNCTION, DB2_SUB, DB2_PROPERTY    ' 2 parse parts  mytype.MyFunction
                              st = AfxStrParse(sElement, 2, ".")   ' Get the MyFunction portion

                              if nLenMatchWord then
                                 if left(ucase(st), nLenMatchWord) = pDoc->sMatchWord THEN 
                                    If ExistsInAutocompleteList(sList, st) = false THEN 
                                       st = st & "?" & str(IMAGE_AUTOC_METHOD) 
                                       sList = sList & st & "|"
                                    end if
                                 end if
                              else
                                 If ExistsInAutocompleteList(sList, st) = false THEN 
                                    st = st & "?" & str(IMAGE_AUTOC_METHOD) 
                                    sList = sList & st & "|"
                                 end if
                              end if         
                        end select      
                     END IF
                  loop
                  iNextType = iNextType + 1
               loop
            END IF
  
         END IF

   end select
     
   sList = trim(sList, any "| ")
   if len(sList) THEN
      ' Save the code editor starting position 
      pDoc->AutoCStartPos = SciExec(hEdit, SCI_GETCURRENTPOS, 0, 0)
      ShowAutoCompletePopup(hEdit, sList)
      return true
   else
      SciExec( hEdit, SCI_AUTOCCANCEL, 0, 0)
   end if 

   function = true
END FUNCTION



