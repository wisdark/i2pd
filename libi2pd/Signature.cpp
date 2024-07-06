/*
* Copyright (c) 2013-2023, The PurpleI2P Project
*
* This file is part of Purple i2pd project and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include <memory>
#include "Log.h"
#include "Signature.h"

namespace i2p
{
namespace crypto
{
#if OPENSSL_EDDSA
	EDDSA25519Verifier::EDDSA25519Verifier ():
		m_Pkey (nullptr)
	{
	}

	EDDSA25519Verifier::~EDDSA25519Verifier ()
	{
		EVP_PKEY_free (m_Pkey);
	}

	void EDDSA25519Verifier::SetPublicKey (const uint8_t * signingKey)
	{
		if (m_Pkey) EVP_PKEY_free (m_Pkey);
		m_Pkey = EVP_PKEY_new_raw_public_key (EVP_PKEY_ED25519, NULL, signingKey, 32);
	}

	bool EDDSA25519Verifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (m_Pkey)
		{	
			EVP_MD_CTX * ctx = EVP_MD_CTX_create ();
			EVP_DigestVerifyInit (ctx, NULL, NULL, NULL, m_Pkey);
			auto ret = EVP_DigestVerify (ctx, signature, 64, buf, len);
			EVP_MD_CTX_destroy (ctx);	
			return ret;	
		}	
		else
			LogPrint (eLogError, "EdDSA verification key is not set");
		return false;
	}

#else
	EDDSA25519Verifier::EDDSA25519Verifier ()
	{
	}

	EDDSA25519Verifier::~EDDSA25519Verifier ()
	{
	}

	void EDDSA25519Verifier::SetPublicKey (const uint8_t * signingKey)
	{
		memcpy (m_PublicKeyEncoded, signingKey, EDDSA25519_PUBLIC_KEY_LENGTH);
		BN_CTX * ctx = BN_CTX_new ();
		m_PublicKey = GetEd25519 ()->DecodePublicKey (m_PublicKeyEncoded, ctx);
		BN_CTX_free (ctx);
	}

	bool EDDSA25519Verifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		uint8_t digest[64];
		SHA512_CTX ctx;
		SHA512_Init (&ctx);
		SHA512_Update (&ctx, signature, EDDSA25519_SIGNATURE_LENGTH/2); // R
		SHA512_Update (&ctx, m_PublicKeyEncoded, EDDSA25519_PUBLIC_KEY_LENGTH); // public key
		SHA512_Update (&ctx, buf, len); // data
		SHA512_Final (digest, &ctx);

		return GetEd25519 ()->Verify (m_PublicKey, digest, signature);
	}
#endif

	EDDSA25519SignerCompat::EDDSA25519SignerCompat (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey)
	{
		// expand key
		Ed25519::ExpandPrivateKey (signingPrivateKey, m_ExpandedPrivateKey);
		// generate and encode public key
		BN_CTX * ctx = BN_CTX_new ();
		auto publicKey = GetEd25519 ()->GeneratePublicKey (m_ExpandedPrivateKey, ctx);
		GetEd25519 ()->EncodePublicKey (publicKey, m_PublicKeyEncoded, ctx);

		if (signingPublicKey && memcmp (m_PublicKeyEncoded, signingPublicKey, EDDSA25519_PUBLIC_KEY_LENGTH))
		{
			// keys don't match, it means older key with 0x1F
			LogPrint (eLogWarning, "Older EdDSA key detected");
			m_ExpandedPrivateKey[EDDSA25519_PRIVATE_KEY_LENGTH - 1] &= 0xDF; // drop third bit
			publicKey = GetEd25519 ()->GeneratePublicKey (m_ExpandedPrivateKey, ctx);
			GetEd25519 ()->EncodePublicKey (publicKey, m_PublicKeyEncoded, ctx);
		}
		BN_CTX_free (ctx);
	}

	EDDSA25519SignerCompat::~EDDSA25519SignerCompat ()
	{
	}

	void EDDSA25519SignerCompat::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		GetEd25519 ()->Sign (m_ExpandedPrivateKey, m_PublicKeyEncoded, buf, len, signature);
	}

#if OPENSSL_EDDSA
	EDDSA25519Signer::EDDSA25519Signer (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey):
		m_Pkey (nullptr), m_Fallback (nullptr)
	{
		m_Pkey = EVP_PKEY_new_raw_private_key (EVP_PKEY_ED25519, NULL, signingPrivateKey, 32);
		uint8_t publicKey[EDDSA25519_PUBLIC_KEY_LENGTH];
		size_t len = EDDSA25519_PUBLIC_KEY_LENGTH;
		EVP_PKEY_get_raw_public_key (m_Pkey, publicKey, &len);
		if (signingPublicKey && memcmp (publicKey, signingPublicKey, EDDSA25519_PUBLIC_KEY_LENGTH))
		{
			LogPrint (eLogWarning, "EdDSA public key mismatch. Fallback");
			m_Fallback = new EDDSA25519SignerCompat (signingPrivateKey, signingPublicKey);
			EVP_PKEY_free (m_Pkey);
			m_Pkey = nullptr;
		}
	}

	EDDSA25519Signer::~EDDSA25519Signer ()
	{
		if (m_Fallback) delete m_Fallback;
		if (m_Pkey) EVP_PKEY_free (m_Pkey);
	}

	void EDDSA25519Signer::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (m_Fallback) 
			return m_Fallback->Sign (buf, len, signature);
		else if (m_Pkey)
		{
				
			EVP_MD_CTX * ctx = EVP_MD_CTX_create ();
			size_t l = 64;
			uint8_t sig[64]; // temporary buffer for signature. openssl issue #7232
			EVP_DigestSignInit (ctx, NULL, NULL, NULL, m_Pkey);
			if (!EVP_DigestSign (ctx, sig, &l, buf, len))
				LogPrint (eLogError, "EdDSA signing failed");
			memcpy (signature, sig, 64);
			EVP_MD_CTX_destroy (ctx);
		}
		else
			LogPrint (eLogError, "EdDSA signing key is not set");
	}
#endif
}
}
