/**************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 *************************************************************/



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_sw.hxx"

#include <tools/stream.hxx>
#include <hintids.hxx>
#include <rtl/tencinfo.h>
#include <sfx2/printer.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/langitem.hxx>
#include <editeng/brkitem.hxx>
#include <editeng/scripttypeitem.hxx>
#include <shellio.hxx>
#include <doc.hxx>
#include <swtypes.hxx>
#include <ndtxt.hxx>
#include <pam.hxx>
#include <frmatr.hxx>
#include <fltini.hxx>
#include <pagedesc.hxx>
#include <breakit.hxx>
#include <swerror.h>
#ifndef _STATSTR_HRC
#include <statstr.hrc> // ResId fuer Statusleiste
#endif
#include <mdiexp.hxx> // ...Percent()
#include <poolfmt.hxx>

#include "vcl/metric.hxx"

#define ASC_BUFFLEN 4096

class SwASCIIParser
{
	SwDoc* pDoc;
	SwPaM* pPam;
	SvStream& rInput;
	sal_Char* pArr;
	const SwAsciiOptions& rOpt;
	SfxItemSet* pItemSet;
	long nFileSize;
	sal_uInt16 nScript;
	bool bNewDoc;

	sal_uLong ReadChars();
	void InsertText( const String& rStr );

public:
	SwASCIIParser( SwDoc* pD, const SwPaM& rCrsr, SvStream& rIn,
							int bReadNewDoc, const SwAsciiOptions& rOpts );
	~SwASCIIParser();

	sal_uLong CallParser();
};


// Aufruf fuer die allg. Reader-Schnittstelle
sal_uLong AsciiReader::Read( SwDoc &rDoc, const String&, SwPaM &rPam, const String & )
{
	if( !pStrm )
	{
		ASSERT( sal_False, "ASCII-Read without stream" );
		return ERR_SWG_READ_ERROR;
	}

	//JP 18.01.96: Alle Ueberschriften sind normalerweise ohne
	//              Kapitelnummer. Darum hier explizit abschalten
	//              weil das Default jetzt wieder auf AN ist.
	if( !bInsertMode )
		Reader::SetNoOutlineNum( rDoc );

	SwASCIIParser* pParser = new SwASCIIParser( &rDoc, rPam, *pStrm,
										!bInsertMode, aOpt.GetASCIIOpts() );
	sal_uLong nRet = pParser->CallParser();

	delete pParser;
	// after Read reset the options
	aOpt.ResetASCIIOpts();
	return nRet;
}

SwASCIIParser::SwASCIIParser(SwDoc* pD, const SwPaM& rCrsr, SvStream& rIn,
	int bReadNewDoc, const SwAsciiOptions& rOpts)
	: pDoc(pD), rInput(rIn), rOpt(rOpts), nScript(0), bNewDoc(bReadNewDoc)
{
	pPam = new SwPaM( *rCrsr.GetPoint() );
	pArr = new sal_Char [ ASC_BUFFLEN + 2 ];

	pItemSet = new SfxItemSet( pDoc->GetAttrPool(),
				RES_CHRATR_FONT,		RES_CHRATR_LANGUAGE,
				RES_CHRATR_CJK_FONT,	RES_CHRATR_CJK_LANGUAGE,
				RES_CHRATR_CTL_FONT,	RES_CHRATR_CTL_LANGUAGE,
				0 );

	// set defaults from the options
	if( rOpt.GetLanguage() )
	{
		SvxLanguageItem aLang( (LanguageType)rOpt.GetLanguage(),
						 		RES_CHRATR_LANGUAGE );
		pItemSet->Put( aLang );
		pItemSet->Put( aLang, RES_CHRATR_CJK_LANGUAGE );
		pItemSet->Put( aLang, RES_CHRATR_CTL_LANGUAGE );
	}
	if( rOpt.GetFontName().Len() )
	{
		Font aTextFont( rOpt.GetFontName(), Size( 0, 10 ) );
		if( pDoc->getPrinter( false ) )
			aTextFont = pDoc->getPrinter( false )->GetFontMetric( aTextFont );
		SvxFontItem aFont( aTextFont.GetFamily(), aTextFont.GetName(),
						   aEmptyStr, aTextFont.GetPitch(), aTextFont.GetCharSet(), RES_CHRATR_FONT );
		pItemSet->Put( aFont );
		pItemSet->Put( aFont, RES_CHRATR_CJK_FONT );
		pItemSet->Put( aFont, RES_CHRATR_CTL_FONT );
	}
}

SwASCIIParser::~SwASCIIParser()
{
	delete pPam;
	delete [] pArr;
	delete pItemSet;
}


// Aufruf des Parsers
sal_uLong SwASCIIParser::CallParser()
{
	rInput.Seek(STREAM_SEEK_TO_END);
	rInput.ResetError();

	nFileSize = rInput.Tell();
	rInput.Seek(STREAM_SEEK_TO_BEGIN);
	rInput.ResetError();

	::StartProgress( STR_STATSTR_W4WREAD, 0, nFileSize, pDoc->GetDocShell() );

	SwPaM* pInsPam = 0;
	xub_StrLen nSttCntnt = 0;
	if (!bNewDoc)
	{
		const SwNodeIndex& rTmp = pPam->GetPoint()->nNode;
		pInsPam = new SwPaM( rTmp, rTmp, 0, -1 );
		nSttCntnt = pPam->GetPoint()->nContent.GetIndex();
	}

	SwTxtFmtColl *pColl = 0;

	if (bNewDoc)
	{
		pColl = pDoc->GetTxtCollFromPool(RES_POOLCOLL_HTML_PRE, false);
		if (!pColl)
			pColl = pDoc->GetTxtCollFromPool(RES_POOLCOLL_STANDARD,false);
		if (pColl)
			pDoc->SetTxtFmtColl(*pPam, pColl);
	}

	sal_uLong nError = ReadChars();

	if( pItemSet )
	{
		// set only the attribute, for scanned scripts.
		if( !( SCRIPTTYPE_LATIN & nScript ))
		{
			pItemSet->ClearItem( RES_CHRATR_FONT );
			pItemSet->ClearItem( RES_CHRATR_LANGUAGE );
		}
		if( !( SCRIPTTYPE_ASIAN & nScript ))
		{
			pItemSet->ClearItem( RES_CHRATR_CJK_FONT );
			pItemSet->ClearItem( RES_CHRATR_CJK_LANGUAGE );
		}
		if( !( SCRIPTTYPE_COMPLEX & nScript ))
		{
			pItemSet->ClearItem( RES_CHRATR_CTL_FONT );
			pItemSet->ClearItem( RES_CHRATR_CTL_LANGUAGE );
		}
		if( pItemSet->Count() )
		{
			if( bNewDoc )
			{
				if (pColl)
				{
					// Using the pool defaults for the font causes significant
					// trouble for the HTML filter, because it is not able
					// to export the pool defaults (or to be more precise:
					// the HTML filter is not able to detect whether a pool
					// default has changed or not. Even a comparison with the
					// HTMLi template does not work, because the defaults are
					// not copied when a new doc is created. The result of
					// comparing pool defaults therefor would be that the
					// defaults are exported always if the have changed for
					// text documents in general. That's not sensible, as well
					// as it is not sensible to export them always.
					sal_uInt16 aWhichIds[4] =
					{
						RES_CHRATR_FONT, RES_CHRATR_CJK_FONT,
						RES_CHRATR_CTL_FONT, 0
					};
					sal_uInt16 *pWhichIds = aWhichIds;
					while (*pWhichIds)
					{
						const SfxPoolItem *pItem;
						if (SFX_ITEM_SET == pItemSet->GetItemState(*pWhichIds,
							false, &pItem))
						{
							pColl->SetFmtAttr( *pItem );
							pItemSet->ClearItem( *pWhichIds );
						}
						++pWhichIds;
					}
				}
				if (pItemSet->Count())
					pDoc->SetDefault(*pItemSet);
			}
			else if( pInsPam )
			{
				// then set over the insert range the defined attributes
				*pInsPam->GetMark() = *pPam->GetPoint();
				pInsPam->GetPoint()->nNode++;
				pInsPam->GetPoint()->nContent.Assign(
									pInsPam->GetCntntNode(), nSttCntnt );

				// !!!!!
				ASSERT( sal_False, "Have to change - hard attr. to para. style" );
				pDoc->InsertItemSet( *pInsPam, *pItemSet, 0 );
			}
		}
		delete pItemSet, pItemSet = 0;
	}

	if( pInsPam )
		delete pInsPam;

	::EndProgress( pDoc->GetDocShell() );
	return nError;
}

sal_uLong SwASCIIParser::ReadChars()
{
	sal_Unicode *pStt = 0, *pEnd = 0, *pLastStt = 0;
	long nReadCnt = 0, nLineLen = 0;
	sal_Unicode cLastCR = 0;
	bool bSwapUnicode = false;

	const SwAsciiOptions *pUseMe=&rOpt;
	SwAsciiOptions aEmpty;
	if (nFileSize >= 2 &&
		aEmpty.GetFontName() == rOpt.GetFontName() &&
		aEmpty.GetCharSet() == rOpt.GetCharSet() &&
		aEmpty.GetLanguage() == rOpt.GetLanguage() &&
		aEmpty.GetParaFlags() == rOpt.GetParaFlags())
	{
		sal_uLong nLen, nOrig;
		nOrig = nLen = rInput.Read(pArr, ASC_BUFFLEN);
		CharSet eCharSet;
		bool bRet = SwIoSystem::IsDetectableText(pArr, nLen, &eCharSet, &bSwapUnicode);
		ASSERT(bRet, "Autodetect of text import without nag dialog must "
			"have failed");
		if (bRet && eCharSet != RTL_TEXTENCODING_DONTKNOW)
		{
			aEmpty.SetCharSet(eCharSet);
			rInput.SeekRel(-(long(nLen)));
		}
		else
			rInput.SeekRel(-(long(nOrig)));
		pUseMe=&aEmpty;
	}

	rtl_TextToUnicodeConverter hConverter=0;
	rtl_TextToUnicodeContext hContext=0;
	CharSet currentCharSet = pUseMe->GetCharSet();
	if (RTL_TEXTENCODING_UCS2 != currentCharSet)
	{
		if( currentCharSet == RTL_TEXTENCODING_DONTKNOW )
				currentCharSet = RTL_TEXTENCODING_ASCII_US;
		hConverter = rtl_createTextToUnicodeConverter( currentCharSet );
		ASSERT( hConverter, "no string convert available" );
		if (!hConverter)
			return ERROR_SW_READ_BASE;
		bSwapUnicode = false;
		hContext = rtl_createTextToUnicodeContext( hConverter );
	}
	else if (pUseMe != &aEmpty) // Already successfully figured out type
	{
		rInput.StartReadingUnicodeText( currentCharSet );
		bSwapUnicode = rInput.IsEndianSwap();
	}

	String sWork;
	sal_uLong nArrOffset = 0;

	do {
		if( pStt >= pEnd )
		{
			if( pLastStt != pStt )
				InsertText( String( pLastStt ));

			// lese einen neuen Block ein
			sal_uLong lGCount;
			if( SVSTREAM_OK != rInput.GetError() || 0 == (lGCount =
						rInput.Read( pArr + nArrOffset,
									 ASC_BUFFLEN - nArrOffset )))
				break;		// aus der WHILE-Schleife heraus

			/*
			#98380#
			If there was some unconverted bytes on the last cycle then they
			were put at the beginning of the array, so total bytes available
			to convert this cycle includes them. If we found 0 following bytes
			then we ignore the previous partial character.
			*/
			lGCount+=nArrOffset;

			if( hConverter )
			{
				sal_uInt32 nInfo;
				sal_Size nNewLen = lGCount, nCntBytes;
				sal_Unicode* pBuf = sWork.AllocBuffer( static_cast< xub_StrLen >(nNewLen) );

				nNewLen = rtl_convertTextToUnicode( hConverter, hContext,
								pArr, lGCount, pBuf, nNewLen,
								(
								RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_DEFAULT |
								RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_DEFAULT |
								RTL_TEXTTOUNICODE_FLAGS_INVALID_DEFAULT |
								RTL_TEXTTOUNICODE_FLAGS_GLOBAL_SIGNATURE
								),
								&nInfo,
								&nCntBytes );
				if( 0 != ( nArrOffset = lGCount - nCntBytes ) )
					memmove( pArr, pArr + nCntBytes, nArrOffset );
				sWork.ReleaseBufferAccess( static_cast< xub_StrLen >(nNewLen) );

				pStt = pLastStt = sWork.GetBufferAccess();
				pEnd = pStt + nNewLen;
			}
			else
			{
				pStt = pLastStt = (sal_Unicode*)pArr;
				pEnd = (sal_Unicode*)(pArr + lGCount);

				if( bSwapUnicode )
				{
					sal_Char* pF = pArr, *pN = pArr + 1;
					for( sal_uLong n = 0; n < lGCount; n += 2, pF += 2, pN += 2 )
					{
						sal_Char c = *pF;
						*pF = *pN;
						*pN = c;
					}
				}
			}

			*pEnd = 0;
			nReadCnt += lGCount;

			::SetProgressState( nReadCnt, pDoc->GetDocShell() );

			if( cLastCR )
			{
				if( 0x0a == *pStt && 0x0d == cLastCR )
					pLastStt = ++pStt;
				cLastCR = 0;
				nLineLen = 0;
				// JP 03.04.96: das letzte am Ende nehmen wir nicht
				if( !rInput.IsEof() || !(pEnd == pStt ||
					( !*pEnd && pEnd == pStt+1 ) ) )
					pDoc->SplitNode( *pPam->GetPoint(), false );
			}
		}

		bool bIns = true, bSplitNode = false;
		switch( *pStt )
		{
//JP 12.11.2001: task 94636 - don't ignore all behind the zero character,
//							  change it to the default "control character"
//		case 0:
//					pEnd = pStt;
//					bIns = false ;
//					break;

		case 0x0a:	if( LINEEND_LF == pUseMe->GetParaFlags() )
					{
						bIns = false;
						*pStt = 0;
						++pStt;

						// JP 03.04.96: das letzte am Ende nehmen wir nicht
						if( !rInput.IsEof() || pEnd != pStt )
							bSplitNode = true;
					}
					break;

		case 0x0d:	if( LINEEND_LF != pUseMe->GetParaFlags() )
					{
						bIns = false;
						*pStt = 0;
						++pStt;

						bool bChkSplit = false;
						if( LINEEND_CRLF == pUseMe->GetParaFlags() )
						{
							if( pStt == pEnd )
								cLastCR = 0x0d;
							else if( 0x0a == *pStt )
							{
								++pStt;
								bChkSplit = true;
							}
						}
						else
							bChkSplit = true;

							// JP 03.04.96: das letzte am Ende nehmen wir nicht
						if( bChkSplit && ( !rInput.IsEof() || pEnd != pStt ))
							bSplitNode = true;
					}
					break;

		case 0x0c:
					{
						// dann mal einen harten Seitenumbruch einfuegen
						*pStt++ = 0;
						if( nLineLen )
						{
							// Change to charset system!!!!
							//rOpt.GetCharSet();
							InsertText( String( pLastStt ));
						}
						pDoc->SplitNode( *pPam->GetPoint(), false );
						pDoc->InsertPoolItem(
							*pPam, SvxFmtBreakItem( SVX_BREAK_PAGE_BEFORE, RES_BREAK ), 0);
						pLastStt = pStt;
						nLineLen = 0;
						bIns = false;
					}
					break;

		case 0x1a:
					if( nReadCnt == nFileSize && pStt+1 == pEnd )
						*pStt = 0;
					else
						*pStt = '#'; // Ersatzdarstellung
					break;

		case '\t':	break;

		default:
			if( ' ' > *pStt )
					// Ctrl-Zchn gefunden ersetze durch '#'
				*pStt = '#';
			break;
		}

		if( bIns )
		{
			if( ( nLineLen >= MAX_ASCII_PARA - 100 ) &&
				( ( *pStt == ' ' ) || ( nLineLen >= MAX_ASCII_PARA - 1 ) ) )
			{
				sal_Unicode c = *pStt;
				*pStt = 0;
				InsertText( String( pLastStt ));
				pDoc->SplitNode( *pPam->GetPoint(), false );
				pLastStt = pStt;
				nLineLen = 0;
				*pStt = c;
			}
			++pStt;
			++nLineLen;
		}
		else if( bSplitNode )
		{
			// es wurde ein CR/LF erkannt, also speichere den Text

			InsertText( String( pLastStt ));
			pDoc->SplitNode( *pPam->GetPoint(), false );
			pLastStt = pStt;
			nLineLen = 0;
		}
	} while(true);

	if( hConverter )
	{
		rtl_destroyTextToUnicodeContext( hConverter, hContext );
		rtl_destroyTextToUnicodeConverter( hConverter );
	}
	return 0;
}

void SwASCIIParser::InsertText( const String& rStr )
{
	pDoc->InsertString( *pPam, rStr );
	if( pItemSet && pBreakIt && nScript != ( SCRIPTTYPE_LATIN |
											 SCRIPTTYPE_ASIAN |
											 SCRIPTTYPE_COMPLEX ) )
		nScript |= pBreakIt->GetAllScriptsOfText( rStr );
}

/* vim: set noet sw=4 ts=4: */
