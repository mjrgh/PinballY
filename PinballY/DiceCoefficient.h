// This file is part of PinballY
// Copyright 2018 Michael J Roberts | GPL v3 or later | NO WARRANTY
//
// Dice's Coefficient for strings
//
// Dice's Coefficient is a measure of the similarity between two sets.
// It's widely used in text processing as a metric for string similarity,
// using the bigrams (adjacent letter pairs) of the strings as the sets
// to be compared.
//
// This module provides a simple implementation that computes the Dice
// Coefficient for a pair of strings.
//

#pragma once
#include <unordered_set>

namespace DiceCoefficient
{
	// we store each bigram as a two-character string
	template<typename chartype> struct Bigram_t
	{
		Bigram_t(chartype a, chartype b) : a(a), b(b) { }

		struct hash
		{
			size_t operator()(Bigram_t const& s) const { return (static_cast<size_t>(s.a) << 16) + static_cast<size_t>(s.b); }
		};
		struct equal_to
		{
			constexpr bool operator()(const Bigram_t& lhs, const Bigram_t &rhs) const { return lhs.a == rhs.a && lhs.b == rhs.b; }
		};

		chartype a, b;
	};
	template<typename chartype> using Bigram = struct Bigram_t<chartype>;
	template<typename chartype> using BigramSet = std::unordered_set<Bigram_t<chartype>, typename Bigram_t<chartype>::hash, typename Bigram_t<chartype>::equal_to>;

	// create a set of bigrams in a string
	template<typename chartype>
	void BuildBigramSet(BigramSet<chartype> &set, const chartype *a)
	{
		// Add a special entry for the first character, in the
		// format <null><first char>.  This adds an extra match
		// for "beginning of string".  We automatically get the
		// mirror image of this - <last char><null> - in the
		// main loop below, since the last character is followed
		// by the trailing null for the whole string.
		set.emplace(0, a[0]);

		// go through each character pair in the string
		for (int i = 0; a[i] != 0; ++i)
			set.emplace(a[i], a[i+1]);
	};

	template<typename chartype>
	float DiceCoefficient(const chartype *a, const chartype *b)
	{
		// the result is zero if either string is of zero length
		if (a[0] == 0 || b[0] == 0)
			return 0.0f;

		// build the bigram set for the strings
		BigramSet<chartype> A, B;
		BuildBigramSet(A, a);
		BuildBigramSet(B, b);

		// figure the coefficient
		return DiceCoefficient(A, B);
	}

	template<typename chartype>
	float DiceCoefficient(const chartype *a, const BigramSet<chartype> &b)
	{
		// the result is zero if either string is of zero length
		if (a[0] == 0 || b[0] == 0)
			return 0.0f;

		// build the bigram set for the string
		BigramSet<chartype> A;
		BuildBigramSet(A, a);

		// figure the coefficient
		return DiceCoefficient(A, b);
	}

	template<typename chartype>
	float DiceCoefficient(const BigramSet<chartype> &a, const BigramSet<chartype> &b)
	{
		// count the bigrams in common
		int nIntersection = 0;
		for (auto const& g : a)
		{
			if (b.find(g) != b.end())
				++nIntersection;
		}

		// the Dice Coefficient is 2 x the number of bigrams in common,
		// divided by the total number of bigrams in the two sets
		return 2.0f * float(nIntersection) / float(a.size() + b.size());
	}
}
