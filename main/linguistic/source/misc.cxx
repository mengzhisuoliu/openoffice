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
#include "precompiled_linguistic.hxx"
#include <tools/string.hxx>
#include <tools/fsys.hxx>
#include <tools/debug.hxx>
#include <unotools/pathoptions.hxx>
#include <svl/lngmisc.hxx>
#include <ucbhelper/content.hxx>
#include <i18npool/mslangid.hxx>
#include <com/sun/star/ucb/XCommandEnvironment.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XFastPropertySet.hpp>
#include <com/sun/star/beans/XPropertyChangeListener.hpp>
#include <com/sun/star/frame/XTerminateListener.hpp>
#include <com/sun/star/frame/XDesktop.hpp>
#include <com/sun/star/frame/XStorable.hpp>

#include <com/sun/star/beans/PropertyValues.hpp>
#include <com/sun/star/uno/Sequence.hxx>
#include <com/sun/star/uno/Reference.h>
#include <com/sun/star/linguistic2/DictionaryType.hpp>
#include <com/sun/star/linguistic2/XSearchableDictionaryList.hpp>
#include <unotools/processfactory.hxx>
#include <unotools/localedatawrapper.hxx>
#include <unotools/syslocale.hxx>

#include <rtl/instance.hxx>

#include "linguistic/misc.hxx"
#include "defs.hxx"
#include "linguistic/lngprops.hxx"
#include "linguistic/hyphdta.hxx"
#include <i18npool/mslangid.hxx>

using namespace utl;
using namespace osl;
using namespace rtl;
using namespace com::sun::star;
using namespace com::sun::star::beans;
using namespace com::sun::star::lang;
using namespace com::sun::star::uno;
using namespace com::sun::star::i18n;
using namespace com::sun::star::linguistic2;

namespace linguistic
{

///////////////////////////////////////////////////////////////////////////

//!! multi-thread safe mutex for all platforms !!
struct LinguMutex : public rtl::Static< osl::Mutex, LinguMutex >
{
};

osl::Mutex &	GetLinguMutex()
{
	return LinguMutex::get();
}

///////////////////////////////////////////////////////////////////////////

LocaleDataWrapper & GetLocaleDataWrapper( sal_Int16 nLang )
{
    static LocaleDataWrapper aLclDtaWrp(
                getProcessServiceFactory(),
                CreateLocale( SvtSysLocale().GetUILanguage() ) );

    const Locale &rLcl = aLclDtaWrp.getLoadedLocale();
    Locale aLcl( CreateLocale( nLang ) );
    if (aLcl.Language != rLcl.Language ||
        aLcl.Country  != rLcl.Country  ||
        aLcl.Variant  != rLcl.Variant)
        aLclDtaWrp.setLocale( aLcl );
    return aLclDtaWrp;
}

///////////////////////////////////////////////////////////////////////////

/**
    returns text-encoding used for ByteString unicode String conversion
 */
rtl_TextEncoding GetTextEncoding( sal_Int16 nLanguage )
{
    DBG_ASSERT( nLanguage != LANGUAGE_NONE, "invalid language argument" );
    static sal_Int16 nLastLanguage = LANGUAGE_NONE;

    // set default value for unknown languages
    static rtl_TextEncoding nEncoding = RTL_TEXTENCODING_DONTKNOW;

    if (nLastLanguage != nLanguage)
    {
        //!! IPR uses textencodings Latin-1, Latin-2, Latin-5 and Latin-7 !!

        nLastLanguage = nLanguage;
        switch (nLanguage)
        {
            case LANGUAGE_GERMAN :
            case LANGUAGE_GERMAN_SWISS :
            case LANGUAGE_ENGLISH_US :
            case LANGUAGE_ENGLISH_UK :
            case LANGUAGE_FRENCH :
            case LANGUAGE_ITALIAN :
            case LANGUAGE_SPANISH :
            case LANGUAGE_CATALAN :
            case LANGUAGE_PORTUGUESE :
            case LANGUAGE_PORTUGUESE_BRAZILIAN :
            case LANGUAGE_DANISH :
            case LANGUAGE_DUTCH :
            case LANGUAGE_SWEDISH :
            case LANGUAGE_FINNISH :
            case LANGUAGE_NORWEGIAN_BOKMAL :
            case LANGUAGE_NORWEGIAN_NYNORSK :
            case LANGUAGE_AFRIKAANS :
            case LANGUAGE_ENGLISH_EIRE :
            case LANGUAGE_ENGLISH_AUS :
#ifdef WNT
                    nEncoding = RTL_TEXTENCODING_MS_1252;   break;
#else
                    nEncoding = RTL_TEXTENCODING_ISO_8859_1;   break;
#endif
            case LANGUAGE_CZECH :
            case LANGUAGE_HUNGARIAN :
            case LANGUAGE_POLISH :
#ifdef WNT
                    nEncoding = RTL_TEXTENCODING_MS_1250;   break;
#else
                    nEncoding = RTL_TEXTENCODING_ISO_8859_2;   break;
#endif
            case LANGUAGE_RUSSIAN :
#ifdef WNT
                    nEncoding = RTL_TEXTENCODING_MS_1251;   break;
#else
                    nEncoding = RTL_TEXTENCODING_ISO_8859_5;   break;
#endif
            case LANGUAGE_GREEK :
#ifdef WNT
                    nEncoding = RTL_TEXTENCODING_MS_1253;   break;
#else
                    nEncoding = RTL_TEXTENCODING_ISO_8859_7;   break;
#endif
            default:
                    DBG_ASSERT( 0, "unexpected language" );
        }
    }

    return nEncoding;
}

///////////////////////////////////////////////////////////////////////////

static inline sal_Int32 Minimum( sal_Int32 n1, sal_Int32 n2, sal_Int32 n3 )
{
    sal_Int32 nMin = n1 < n2 ? n1 : n2;
    return nMin < n3 ? nMin : n3;
}

///////////////////////////////////////////////////////////////////////////

class IntArray2D
{
private:
    sal_Int32  *pData;
    int         n1, n2;

public:
    IntArray2D( int nDim1, int nDim2 );
    ~IntArray2D();

    sal_Int32 & Value( int i, int k  );
};

IntArray2D::IntArray2D( int nDim1, int nDim2 )
{
    n1 = nDim1;
    n2 = nDim2;
    pData = new sal_Int32[n1 * n2];
}

IntArray2D::~IntArray2D()
{
    delete[] pData;
}

sal_Int32 & IntArray2D::Value( int i, int k  )
{
    DBG_ASSERT( 0 <= i && i < n1, "first index out of range" );
    DBG_ASSERT( 0 <= k && k < n2, "first index out of range" );
    DBG_ASSERT( i * n2 + k < n1 * n2, "index out of range" );
    return pData[ i * n2 + k ];
}


sal_Int32 LevDistance( const OUString &rTxt1, const OUString &rTxt2 )
{
    sal_Int32 nLen1 = rTxt1.getLength();
    sal_Int32 nLen2 = rTxt2.getLength();

    if (nLen1 == 0)
        return nLen2;
    if (nLen2 == 0)
        return nLen1;

    IntArray2D aData( nLen1 + 1, nLen2 + 1 );

    sal_Int32 i, k;
    for (i = 0;  i <= nLen1;  ++i)
        aData.Value(i, 0) = i;
    for (k = 0;  k <= nLen2;  ++k)
        aData.Value(0, k) = k;
    for (i = 1;  i <= nLen1;  ++i)
    {
        for (k = 1;  k <= nLen2;  ++k)
        {
            sal_Unicode c1i = rTxt1.getStr()[i - 1];
            sal_Unicode c2k = rTxt2.getStr()[k - 1];
            sal_Int32 nCost = c1i == c2k ? 0 : 1;
            sal_Int32 nNew = Minimum( aData.Value(i-1, k  ) + 1,
                                       aData.Value(i  , k-1) + 1,
                                       aData.Value(i-1, k-1) + nCost );
            // take transposition (exchange with left or right char) in account
            if (2 < i && 2 < k)
            {
                int nT = aData.Value(i-2, k-2) + 1;
                if (rTxt1.getStr()[i - 2] != c1i)
                    ++nT;
                if (rTxt2.getStr()[k - 2] != c2k)
                    ++nT;
                if (nT < nNew)
                    nNew = nT;
            }

            aData.Value(i, k) = nNew;
        }
    }
    sal_Int32 nDist = aData.Value(nLen1, nLen2);
    return nDist;
 }

///////////////////////////////////////////////////////////////////////////

sal_Bool IsUseDicList( const PropertyValues &rProperties,
		const uno::Reference< XPropertySet > &rxProp )
{
	sal_Bool bRes = sal_True;

	sal_Int32 nLen = rProperties.getLength();
	const PropertyValue *pVal = rProperties.getConstArray();
	sal_Int32 i;

	for ( i = 0;  i < nLen;  ++i)
	{
		if (UPH_IS_USE_DICTIONARY_LIST == pVal[i].Handle)
		{
			pVal[i].Value >>= bRes;
			break;
		}
	}
	if (i >= nLen)	// no temporary value found in 'rProperties'
	{
		uno::Reference< XFastPropertySet > xFast( rxProp, UNO_QUERY );
		if (xFast.is())
			xFast->getFastPropertyValue( UPH_IS_USE_DICTIONARY_LIST ) >>= bRes;
	}

	return bRes;
}


sal_Bool IsIgnoreControlChars( const PropertyValues &rProperties,
		const uno::Reference< XPropertySet > &rxProp )
{
	sal_Bool bRes = sal_True;

	sal_Int32 nLen = rProperties.getLength();
	const PropertyValue *pVal = rProperties.getConstArray();
	sal_Int32 i;

	for ( i = 0;  i < nLen;  ++i)
	{
		if (UPH_IS_IGNORE_CONTROL_CHARACTERS == pVal[i].Handle)
		{
			pVal[i].Value >>= bRes;
			break;
		}
	}
	if (i >= nLen)	// no temporary value found in 'rProperties'
	{
		uno::Reference< XFastPropertySet > xFast( rxProp, UNO_QUERY );
		if (xFast.is())
			xFast->getFastPropertyValue( UPH_IS_IGNORE_CONTROL_CHARACTERS ) >>= bRes;
	}

	return bRes;
}


static sal_Bool lcl_HasHyphInfo( const uno::Reference<XDictionaryEntry> &xEntry )
{
	sal_Bool bRes = sal_False;
	if (xEntry.is())
	{
		// there has to be (at least one) '=' denoting a hyphenation position
		// and it must not be before any character of the word
		sal_Int32 nIdx = xEntry->getDictionaryWord().indexOf( '=' );
		bRes = nIdx != -1  &&  nIdx != 0;
	}
	return bRes;
}


uno::Reference< XDictionaryEntry > SearchDicList(
		const uno::Reference< XDictionaryList > &xDicList,
		const OUString &rWord, sal_Int16 nLanguage,
		sal_Bool bSearchPosDics, sal_Bool bSearchSpellEntry )
{
	MutexGuard	aGuard( GetLinguMutex() );

	uno::Reference< XDictionaryEntry > xEntry;

	if (!xDicList.is())
		return xEntry;

	const uno::Sequence< uno::Reference< XDictionary > >
			aDics( xDicList->getDictionaries() );
	const uno::Reference< XDictionary >
			*pDic = aDics.getConstArray();
	sal_Int32 nDics = xDicList->getCount();

	sal_Int32 i;
	for (i = 0;  i < nDics;  i++)
	{
		uno::Reference< XDictionary > axDic( pDic[i], UNO_QUERY );

		DictionaryType	eType = axDic->getDictionaryType();
		sal_Int16			nLang = LocaleToLanguage( axDic->getLocale() );

		if ( axDic.is() && axDic->isActive()
			&& (nLang == nLanguage  ||  nLang == LANGUAGE_NONE) )
		{
			DBG_ASSERT( eType != DictionaryType_MIXED,
				"lng : unexpected dictionary type" );

			if (   (!bSearchPosDics  &&  eType == DictionaryType_NEGATIVE)
				|| ( bSearchPosDics  &&  eType == DictionaryType_POSITIVE))
			{
				if ( (xEntry = axDic->getEntry( rWord )).is() )
				{
					if (bSearchSpellEntry || lcl_HasHyphInfo( xEntry ))
						break;
                }
                xEntry = 0;
			}
		}
	}

	return xEntry;
}


sal_Bool SaveDictionaries( const uno::Reference< XDictionaryList > &xDicList )
{
    if (!xDicList.is())
        return sal_True;

    sal_Bool bRet = sal_True;

    Sequence< uno::Reference< XDictionary >  > aDics( xDicList->getDictionaries() );
    const uno::Reference< XDictionary >  *pDic = aDics.getConstArray();
    sal_Int32 nCount = aDics.getLength();
    for (sal_Int32 i = 0;  i < nCount;  i++)
    {
        try
        {
            uno::Reference< frame::XStorable >  xStor( pDic[i], UNO_QUERY );
            if (xStor.is())
            {
                if (!xStor->isReadonly() && xStor->hasLocation())
                    xStor->store();
            }
        }
        catch(uno::Exception &)
        {
            bRet = sal_False;
        }
    }

    return bRet;
}


sal_uInt8 AddEntryToDic(
        uno::Reference< XDictionary >  &rxDic,
        const OUString &rWord, sal_Bool bIsNeg,
        const OUString &rRplcTxt, sal_Int16 /* nRplcLang */,
        sal_Bool bStripDot )
{
    if (!rxDic.is())
        return DIC_ERR_NOT_EXISTS;

    OUString aTmp( rWord );
    if (bStripDot)
    {
        sal_Int32 nLen = rWord.getLength();
        if (nLen > 0  &&  '.' == rWord[ nLen - 1])
        {
            // remove trailing '.'
            // (this is the official way to do this :-( )
            aTmp = aTmp.copy( 0, nLen - 1 );
        }
    }
    sal_Bool bAddOk = rxDic->add( aTmp, bIsNeg, rRplcTxt );

    sal_uInt8 nRes = DIC_ERR_NONE;
    if (!bAddOk)
    {
        if (rxDic->isFull())
            nRes = DIC_ERR_FULL;
        else
        {
            uno::Reference< frame::XStorable >  xStor( rxDic, UNO_QUERY );
            if (xStor.is() && xStor->isReadonly())
                nRes = DIC_ERR_READONLY;
            else
                nRes = DIC_ERR_UNKNOWN;
        }
    }

    return nRes;
}


///////////////////////////////////////////////////////////////////////////

LanguageType LocaleToLanguage( const Locale& rLocale )
{
	//	empty Locale -> LANGUAGE_NONE
	if ( rLocale.Language.getLength() == 0 )
		return LANGUAGE_NONE;

	return MsLangId::convertLocaleToLanguage( rLocale );
}


Locale& LanguageToLocale( Locale& rLocale, LanguageType eLang )
{
	if ( eLang != LANGUAGE_NONE	/* &&  eLang != LANGUAGE_SYSTEM */)
		MsLangId::convertLanguageToLocale( eLang, rLocale );

	return rLocale;
}

Locale CreateLocale( LanguageType eLang )
{
	Locale aLocale;
	if ( eLang != LANGUAGE_NONE /* &&  eLang != LANGUAGE_SYSTEM */)
		return MsLangId::convertLanguageToLocale( eLang );

	return aLocale;
}

uno::Sequence< Locale > LangSeqToLocaleSeq( const uno::Sequence< sal_Int16 > &rLangSeq )
{
    const sal_Int16 *pLang = rLangSeq.getConstArray();
	sal_Int32 nCount = rLangSeq.getLength();

	uno::Sequence< Locale > aLocales( nCount );
	Locale *pLocale = aLocales.getArray();
	for (sal_Int32 i = 0;  i < nCount;  ++i)
	{
		LanguageToLocale( pLocale[i], pLang[ i ] );
	}

	return aLocales;
}

uno::Sequence< sal_Int16 >
	LocaleSeqToLangSeq( uno::Sequence< Locale > &rLocaleSeq )
{
	const Locale *pLocale = rLocaleSeq.getConstArray();
	sal_Int32 nCount = rLocaleSeq.getLength();

    uno::Sequence< sal_Int16 >   aLangs( nCount );
    sal_Int16 *pLang = aLangs.getArray();
	for (sal_Int32 i = 0;  i < nCount;  ++i)
	{
		pLang[i] = LocaleToLanguage( pLocale[i] );
	}

	return aLangs;
}

///////////////////////////////////////////////////////////////////////////

sal_Bool    IsReadOnly( const String &rURL, sal_Bool *pbExist )
{
    sal_Bool bRes = sal_False;
    sal_Bool bExists = sal_False;

    if (rURL.Len() > 0)
    {
        try
        {
            uno::Reference< ::com::sun::star::ucb::XCommandEnvironment > xCmdEnv;
            ::ucbhelper::Content aContent( rURL, xCmdEnv );

            bExists = aContent.isDocument();
            if (bExists)
            {
                Any aAny( aContent.getPropertyValue( A2OU( "IsReadOnly" ) ) );
                aAny >>= bRes;
            }
        }
        catch (Exception &)
        {
            bRes = sal_True;
        }
    }

    if (pbExist)
        *pbExist = bExists;
    return bRes;
}

///////////////////////////////////////////////////////////////////////////


static sal_Bool GetAltSpelling( sal_Int16 &rnChgPos, sal_Int16 &rnChgLen, OUString &rRplc,
		uno::Reference< XHyphenatedWord > &rxHyphWord )
{
	sal_Bool bRes = rxHyphWord->isAlternativeSpelling();
	if (bRes)
	{
		OUString aWord( rxHyphWord->getWord() ),
			 	aHyphenatedWord( rxHyphWord->getHyphenatedWord() );
        sal_Int16   nHyphenationPos     = rxHyphWord->getHyphenationPos();
        /*sal_Int16   nHyphenPos          = rxHyphWord->getHyphenPos()*/;
		const sal_Unicode *pWord	= aWord.getStr(),
					  	*pAltWord = aHyphenatedWord.getStr();

		// at least char changes directly left or right to the hyphen
		// should(!) be handled properly...
		//! nHyphenationPos and nHyphenPos differ at most by 1 (see above)
		//! Beware: eg "Schiffahrt" in German (pre spelling reform)
		//! proves to be a bit nasty (nChgPosLeft and nChgPosRight overlap
		//! to an extend.)

		// find first different char from left
		sal_Int32	nPosL    = 0,
					nAltPosL = 0;
		for (sal_Int16 i = 0 ;  pWord[ nPosL ] == pAltWord[ nAltPosL ];  nPosL++, nAltPosL++, i++)
		{
			// restrict changes area beginning to the right to
			// the char immediately following the hyphen.
			//! serves to insert the additional "f" in "Schiffahrt" at
			//! position 5 rather than position 6.
			if (i >= nHyphenationPos + 1)
				break;
		}

		// find first different char from right
		sal_Int32	nPosR 	 = aWord.getLength() - 1,
					nAltPosR = aHyphenatedWord.getLength() - 1;
		for ( ;  nPosR >= nPosL  &&  nAltPosR >= nAltPosL
					&&  pWord[ nPosR ] == pAltWord[ nAltPosR ];
				nPosR--, nAltPosR--)
			;

        rnChgPos = sal::static_int_cast< sal_Int16 >(nPosL);
        rnChgLen = sal::static_int_cast< sal_Int16 >(nPosR - nPosL + 1);
		DBG_ASSERT( rnChgLen >= 0, "nChgLen < 0");

		sal_Int32 nTxtStart = nPosL;
		sal_Int32 nTxtLen   = nAltPosL - nPosL + 1;
		rRplc = aHyphenatedWord.copy( nTxtStart, nTxtLen );
	}
	return bRes;
}


static sal_Int16 GetOrigWordPos( const OUString &rOrigWord, sal_Int16 nPos )
{
    sal_Int32 nLen = rOrigWord.getLength();
    sal_Int32 i = -1;
    while (nPos >= 0  &&  i++ < nLen)
	{
		sal_Unicode cChar = rOrigWord[i];
        sal_Bool bSkip = IsHyphen( cChar ) || IsControlChar( cChar );
        if (!bSkip)
            --nPos;
	}
    return sal::static_int_cast< sal_Int16 >((0 <= i  &&  i < nLen) ? i : -1);
}


sal_Int32 GetPosInWordToCheck( const OUString &rTxt, sal_Int32 nPos )
{
	sal_Int32 nRes = -1;
	sal_Int32 nLen = rTxt.getLength();
	if (0 <= nPos  &&  nPos < nLen)
	{
        nRes = 0;
        for (sal_Int32 i = 0;  i < nPos;  ++i)
		{
            sal_Unicode cChar = rTxt[i];
            sal_Bool bSkip = IsHyphen( cChar ) || IsControlChar( cChar );
            if (!bSkip)
                ++nRes;
		}
	}
	return nRes;
}


uno::Reference< XHyphenatedWord > RebuildHyphensAndControlChars(
		const OUString &rOrigWord,
		uno::Reference< XHyphenatedWord > &rxHyphWord )
{
	uno::Reference< XHyphenatedWord > xRes;
	if (rOrigWord.getLength() && rxHyphWord.is())
	{
		sal_Int16 	 nChgPos = 0,
				 nChgLen = 0;
		OUString aRplc;
		sal_Bool bAltSpelling = GetAltSpelling( nChgPos, nChgLen, aRplc, rxHyphWord );
#if OSL_DEBUG_LEVEL > 1
        OUString aWord( rxHyphWord->getWord() );
#endif

		OUString aOrigHyphenatedWord;
        sal_Int16 nOrigHyphenPos        = -1;
        sal_Int16 nOrigHyphenationPos   = -1;
		if (!bAltSpelling)
		{
			aOrigHyphenatedWord = rOrigWord;
            nOrigHyphenPos      = GetOrigWordPos( rOrigWord, rxHyphWord->getHyphenPos() );
            nOrigHyphenationPos = GetOrigWordPos( rOrigWord, rxHyphWord->getHyphenationPos() );
		}
		else
		{
            //! should at least work with the German words
            //! B�-c-k-er and Sc-hif-fah-rt

			OUString aLeft, aRight;
            sal_Int16 nPos = GetOrigWordPos( rOrigWord, nChgPos );

            // get words like Sc-hif-fah-rt to work correct
            sal_Int16 nHyphenationPos = rxHyphWord->getHyphenationPos();
            if (nChgPos > nHyphenationPos)
                --nPos;

            aLeft = rOrigWord.copy( 0, nPos );
            aRight = rOrigWord.copy( nPos + nChgLen );

			aOrigHyphenatedWord =  aLeft;
			aOrigHyphenatedWord += aRplc;
			aOrigHyphenatedWord += aRight;

            nOrigHyphenPos      = sal::static_int_cast< sal_Int16 >(aLeft.getLength() +
                                  rxHyphWord->getHyphenPos() - nChgPos);
            nOrigHyphenationPos = GetOrigWordPos( rOrigWord, nHyphenationPos );
		}

        if (nOrigHyphenPos == -1  ||  nOrigHyphenationPos == -1)
		{
            DBG_ASSERT( 0, "failed to get nOrigHyphenPos or nOrigHyphenationPos" );
		}
		else
		{
			sal_Int16 nLang = LocaleToLanguage( rxHyphWord->getLocale() );
			xRes = new HyphenatedWord(
                        rOrigWord, nLang, nOrigHyphenationPos,
						aOrigHyphenatedWord, nOrigHyphenPos );
		}

	}
	return xRes;
}


///////////////////////////////////////////////////////////////////////////


static CharClass & lcl_GetCharClass()
{
    static CharClass aCC( CreateLocale( LANGUAGE_ENGLISH_US ) );
    return aCC;
}


osl::Mutex & lcl_GetCharClassMutex()
{
	static osl::Mutex	aMutex;
	return aMutex;
}


sal_Bool IsUpper( const String &rText, xub_StrLen nPos, xub_StrLen nLen, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    sal_Int32 nFlags = rCC.getStringType( rText, nPos, nLen );
	return 		(nFlags & KCharacterType::UPPER)
			&& !(nFlags & KCharacterType::LOWER);
}


sal_Bool IsLower( const String &rText, xub_StrLen nPos, xub_StrLen nLen, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    sal_Int32 nFlags = rCC.getStringType( rText, nPos, nLen );
    return      (nFlags & KCharacterType::LOWER)
            && !(nFlags & KCharacterType::UPPER);
}


String ToLower( const String &rText, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    return rCC.lower( rText );
}


String ToUpper( const String &rText, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    return rCC.upper( rText );
}


String ToTitle( const String &rText, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    return rCC.toTitle( rText, 0, rText.Len() );
}


sal_Unicode	ToLower( const sal_Unicode cChar, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    return rCC.lower( cChar ).GetChar(0);
}


sal_Unicode	ToUpper( const sal_Unicode cChar, sal_Int16 nLanguage )
{
    MutexGuard  aGuard( lcl_GetCharClassMutex() );

    CharClass &rCC = lcl_GetCharClass();
    rCC.setLocale( CreateLocale( nLanguage ) );
    return rCC.upper( cChar ).GetChar(0);
}

// sorted(!) array of unicode ranges for code points that are exclusively(!) used as numbers
// and thus may NOT not be part of names or words like the Chinese/Japanese number characters
static const sal_uInt32 the_aDigitZeroes [] =
{
    0x00000030, //0039    ; Decimal # Nd  [10] DIGIT ZERO..DIGIT NINE
    0x00000660, //0669    ; Decimal # Nd  [10] ARABIC-INDIC DIGIT ZERO..ARABIC-INDIC DIGIT NINE
    0x000006F0, //06F9    ; Decimal # Nd  [10] EXTENDED ARABIC-INDIC DIGIT ZERO..EXTENDED ARABIC-INDIC DIGIT NINE
    0x000007C0, //07C9    ; Decimal # Nd  [10] NKO DIGIT ZERO..NKO DIGIT NINE
    0x00000966, //096F    ; Decimal # Nd  [10] DEVANAGARI DIGIT ZERO..DEVANAGARI DIGIT NINE
    0x000009E6, //09EF    ; Decimal # Nd  [10] BENGALI DIGIT ZERO..BENGALI DIGIT NINE
    0x00000A66, //0A6F    ; Decimal # Nd  [10] GURMUKHI DIGIT ZERO..GURMUKHI DIGIT NINE
    0x00000AE6, //0AEF    ; Decimal # Nd  [10] GUJARATI DIGIT ZERO..GUJARATI DIGIT NINE
    0x00000B66, //0B6F    ; Decimal # Nd  [10] ORIYA DIGIT ZERO..ORIYA DIGIT NINE
    0x00000BE6, //0BEF    ; Decimal # Nd  [10] TAMIL DIGIT ZERO..TAMIL DIGIT NINE
    0x00000C66, //0C6F    ; Decimal # Nd  [10] TELUGU DIGIT ZERO..TELUGU DIGIT NINE
    0x00000CE6, //0CEF    ; Decimal # Nd  [10] KANNADA DIGIT ZERO..KANNADA DIGIT NINE
    0x00000D66, //0D6F    ; Decimal # Nd  [10] MALAYALAM DIGIT ZERO..MALAYALAM DIGIT NINE
    0x00000E50, //0E59    ; Decimal # Nd  [10] THAI DIGIT ZERO..THAI DIGIT NINE
    0x00000ED0, //0ED9    ; Decimal # Nd  [10] LAO DIGIT ZERO..LAO DIGIT NINE
    0x00000F20, //0F29    ; Decimal # Nd  [10] TIBETAN DIGIT ZERO..TIBETAN DIGIT NINE
    0x00001040, //1049    ; Decimal # Nd  [10] MYANMAR DIGIT ZERO..MYANMAR DIGIT NINE
    0x00001090, //1099    ; Decimal # Nd  [10] MYANMAR SHAN DIGIT ZERO..MYANMAR SHAN DIGIT NINE
    0x000017E0, //17E9    ; Decimal # Nd  [10] KHMER DIGIT ZERO..KHMER DIGIT NINE
    0x00001810, //1819    ; Decimal # Nd  [10] MONGOLIAN DIGIT ZERO..MONGOLIAN DIGIT NINE
    0x00001946, //194F    ; Decimal # Nd  [10] LIMBU DIGIT ZERO..LIMBU DIGIT NINE
    0x000019D0, //19D9    ; Decimal # Nd  [10] NEW TAI LUE DIGIT ZERO..NEW TAI LUE DIGIT NINE
    0x00001B50, //1B59    ; Decimal # Nd  [10] BALINESE DIGIT ZERO..BALINESE DIGIT NINE
    0x00001BB0, //1BB9    ; Decimal # Nd  [10] SUNDANESE DIGIT ZERO..SUNDANESE DIGIT NINE
    0x00001C40, //1C49    ; Decimal # Nd  [10] LEPCHA DIGIT ZERO..LEPCHA DIGIT NINE
    0x00001C50, //1C59    ; Decimal # Nd  [10] OL CHIKI DIGIT ZERO..OL CHIKI DIGIT NINE
    0x0000A620, //A629    ; Decimal # Nd  [10] VAI DIGIT ZERO..VAI DIGIT NINE
    0x0000A8D0, //A8D9    ; Decimal # Nd  [10] SAURASHTRA DIGIT ZERO..SAURASHTRA DIGIT NINE
    0x0000A900, //A909    ; Decimal # Nd  [10] KAYAH LI DIGIT ZERO..KAYAH LI DIGIT NINE
    0x0000AA50, //AA59    ; Decimal # Nd  [10] CHAM DIGIT ZERO..CHAM DIGIT NINE
    0x0000FF10, //FF19    ; Decimal # Nd  [10] FULLWIDTH DIGIT ZERO..FULLWIDTH DIGIT NINE
    0x000104A0, //104A9   ; Decimal # Nd  [10] OSMANYA DIGIT ZERO..OSMANYA DIGIT NINE
    0x0001D7CE  //1D7FF   ; Decimal # Nd  [50] MATHEMATICAL BOLD DIGIT ZERO..MATHEMATICAL MONOSPACE DIGIT NINE
};

sal_Bool HasDigits( const OUString &rText )
{
    static const int nNumDigitZeroes = sizeof(the_aDigitZeroes) / sizeof(the_aDigitZeroes[0]);
    const sal_Int32 nLen = rText.getLength();

    sal_Int32 i = 0;
    while (i < nLen) // for all characters ...
    {
		const sal_uInt32 nCodePoint = rText.iterateCodePoints( &i );    // handle unicode surrogates correctly...
        for (int j = 0; j < nNumDigitZeroes; ++j)   // ... check in all 0..9 ranges
        {
			sal_uInt32 nDigitZero = the_aDigitZeroes[ j ];
			if (nDigitZero > nCodePoint)
				break;
			if (/*nDigitZero <= nCodePoint &&*/ nCodePoint <= nDigitZero + 9)
				return sal_True;
        }
    }
    return sal_False;
}


sal_Bool IsNumeric( const String &rText )
{
	sal_Bool bRes = sal_False;
	xub_StrLen nLen = rText.Len();
	if (nLen)
	{
		bRes = sal_True;
		xub_StrLen i = 0;
		while (i < nLen)
		{
			sal_Unicode cChar = rText.GetChar( i++ );
			if ( !((sal_Unicode)'0' <= cChar  &&  cChar <= (sal_Unicode)'9') )
			{
				bRes = sal_False;
				break;
			}
		}
	}
	return bRes;
}


///////////////////////////////////////////////////////////////////////////

uno::Reference< XInterface > GetOneInstanceService( const char *pServiceName )
{
	uno::Reference< XInterface > xRef;

	if (pServiceName)
	{
		uno::Reference< XMultiServiceFactory >  xMgr( getProcessServiceFactory() );
		if (xMgr.is())
		{
			try
			{
				xRef = xMgr->createInstance( A2OU( pServiceName ) );
			}
			catch (uno::Exception &)
			{
                DBG_ASSERT( 0, "createInstance failed" );
			}
		}
	}

	return xRef;
}

uno::Reference< XPropertySet > GetLinguProperties()
{
	return uno::Reference< XPropertySet > (
		GetOneInstanceService( SN_LINGU_PROPERTIES ), UNO_QUERY );
}

uno::Reference< XSearchableDictionaryList > GetSearchableDictionaryList()
{
	return uno::Reference< XSearchableDictionaryList > (
		GetOneInstanceService( SN_DICTIONARY_LIST ), UNO_QUERY );
}

uno::Reference< XDictionaryList > GetDictionaryList()
{
	return uno::Reference< XDictionaryList > (
		GetOneInstanceService( SN_DICTIONARY_LIST ), UNO_QUERY );
}

uno::Reference< XDictionary > GetIgnoreAllList()
{
    uno::Reference< XDictionary > xRes;
    uno::Reference< XDictionaryList > xDL( GetDictionaryList() );
    if (xDL.is())
        xRes = xDL->getDictionaryByName( A2OU("IgnoreAllList") );
    return xRes;
}

///////////////////////////////////////////////////////////////////////////

AppExitListener::AppExitListener()
{
	// add object to Desktop EventListeners in order to properly call
	// the AtExit function at application exit.
	uno::Reference< XMultiServiceFactory > xMgr = getProcessServiceFactory();

	if (xMgr.is())
	{
		try
		{
			xDesktop = uno::Reference< frame::XDesktop >(
					xMgr->createInstance( A2OU( SN_DESKTOP ) ), UNO_QUERY );
		}
		catch (uno::Exception &)
		{
            DBG_ASSERT( 0, "createInstance failed" );
		}
	}
}

AppExitListener::~AppExitListener()
{
}


void AppExitListener::Activate()
{
	if (xDesktop.is())
		xDesktop->addTerminateListener( this );
}


void AppExitListener::Deactivate()
{
	if (xDesktop.is())
		xDesktop->removeTerminateListener( this );
}


void SAL_CALL
	AppExitListener::disposing( const EventObject& rEvtSource )
		throw(RuntimeException)
{
	MutexGuard	aGuard( GetLinguMutex() );

	if (xDesktop.is()  &&  rEvtSource.Source == xDesktop)
	{
		xDesktop = NULL;	//! release reference to desktop
	}
}


void SAL_CALL
    AppExitListener::queryTermination( const EventObject& /*rEvtSource*/ )
		throw(frame::TerminationVetoException, RuntimeException)
{
	//MutexGuard	aGuard( GetLinguMutex() );
}


void SAL_CALL
	AppExitListener::notifyTermination( const EventObject& rEvtSource )
		throw(RuntimeException)
{
	MutexGuard	aGuard( GetLinguMutex() );

	if (xDesktop.is()  &&  rEvtSource.Source == xDesktop)
	{
		AtExit();
	}
}

///////////////////////////////////////////////////////////////////////////

}	// namespace linguistic

