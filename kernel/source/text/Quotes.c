
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Quotes

\************************************************************************/

#include "text/Quotes.h"
#include "math/Math.h"

/************************************************************************/

LPCSTR Quotes [] = {
    TEXT("\"Nearly all men can stand adversity, but if you want to test a man's character, give him power.\" Abraham Lincoln."),
    TEXT("\"Those who deny freedom to others deserve it not for themselves.\" (speech, 1859). Abraham Lincoln."),
    TEXT("\"Reality is merely an illusion, albeit a very persistent one.\" Letter to Michele Besso (1955). Albert Einstein."),
    TEXT("\"We do not see things as they are, we see them as we are.\" (attribution debated). Anaïs Nin."),
    TEXT("\"Knowing yourself is the beginning of all wisdom.\" Aristotle."),
    TEXT("\"Wisdom begins in wonder.\" Metaphysics. Aristotle."),
    TEXT("\"It is the mark of an educated mind to be able to entertain a thought without accepting it.\" Aristotle."),
    TEXT("\"We are what we repeatedly do. Excellence, then, is not an act, but a habit.\" Nicomachean Ethics. Aristotle."),
    TEXT("\"He who would give up essential liberty to purchase a little temporary safety deserves neither liberty nor safety.\" (1755). Benjamin Franklin."),
    TEXT("\"Listen to their words and observe their actions.\" The Analects. Confucius."),
    TEXT("\"Holding on to anger is like grasping a hot coal with the intent of throwing it at someone else; you are the one who gets burned.\" (Buddhist teaching). Gautama Buddha."),
    TEXT("\"One cannot understand modern civilization unless one first admits that it is a universal conspiracy against every form of inner life.\" France Against the Robots. Georges Bernanos."),
    TEXT("\"The eye sees only what the mind is prepared to comprehend.\" (attribution uncertain). Henri Bergson."),
    TEXT("\"Therefore I speak to them in parables: because seeing they see not; and hearing they hear not, neither do they understand.\" The Gospel of Matthew (13:13). Jesus Christ."),
    TEXT("\"Silence is a source of great strength.\" Lao Tzu."),
    TEXT("\"He who knows others is wise; he who knows himself is enlightened.\" Tao Te Ching. Lao Tzu."),
    TEXT("\"The journey of a thousand miles begins with one step.\" Tao Te Ching. Lao Tzu."),
    TEXT("\"Where love is absent, there hatred grows.\" Leo Tolstoy."),
    TEXT("\"Power tends to corrupt, and absolute power corrupts absolutely.\" Letter to Bishop Mandell Creighton (1887). Lord Acton."),
    TEXT("\"Waste no more time arguing what a good man should be. Be one.\" Meditations. Marcus Aurelius."),
    TEXT("\"The happiness of your life depends upon the quality of your thoughts.\" Meditations. Marcus Aurelius."),
    TEXT("\"Darkness cannot drive out darkness; only light can do that. Hate cannot drive out hate; only love can do that.\" Strength to Love. Martin Luther King Jr."),
    TEXT("\"He who has a why to live can bear almost any how.\" Twilight of the Idols. Friedrich Nietzsche."),
    TEXT("\"The only true wisdom is in knowing you know nothing.\" Apology. Socrates."),
    TEXT("\"When the people fear the government, there is tyranny; when the government fears the people, there is liberty.\" Thomas Jefferson."),
    TEXT("\"The greatest tyrannies are always perpetrated in the name of the noblest causes.\" Thomas Paine."),
    TEXT("\"Those who can make you believe absurdities can make you commit atrocities.\" Questions sur les miracles. Voltaire."),
    TEXT("\"It is dangerous to be right in matters on which the established authorities are wrong.\" The Age of Louis XIV. Voltaire."),
    TEXT("\"I disapprove of what you say, but I will defend to the death your right to say it.\" Voltaire."),
    TEXT("\"There are more things in heaven and earth, Horatio, than are dreamt of in your philosophy.\" Hamlet. William Shakespeare."),
    TEXT("\"But the line dividing good and evil cuts through the heart of every human being.\" The Gulag Archipelago. Aleksandr Solzhenitsyn."),
    TEXT("\"Hypocrisy is a tribute vice pays to virtue.\" Maxims. François de La Rochefoucauld."),
    TEXT("\"When you wake up in the morning, tell yourself: The people I deal with today will be meddling, ungrateful, arrogant, dishonest, jealous, and surly.\" Meditations. Marcus Aurelius."),
    TEXT("\"Action is the pointer which shows the balance. We must not touch the pointer but the weight.\" Gravity and Grace. Simone Weil."),
    TEXT("\"Political parties are a wonderful mechanism for killing the truth.\" On the Abolition of All Political Parties. Simone Weil."),
    TEXT("\"The world will not be destroyed by those who do evil, but by those who watch them without doing anything.\" Albert Einstein."),
    TEXT("\"Unthinking respect for authority is the greatest enemy of truth.\" (1931). Albert Einstein."),
    TEXT("\"Imagination is more important than knowledge.\" (1929 interview). Albert Einstein."),
    TEXT("\"The important thing is not to stop questioning. Curiosity has its own reason for existing.\" Albert Einstein."),
    TEXT("\"Try not to become a man of success, but rather try to become a man of value.\" Albert Einstein."),
    TEXT("\"Insanity is doing the same thing over and over again and expecting different results.\" Albert Einstein."),
    TEXT("\"In our country the lie has become not just a moral category but a pillar of the State.\" The Gulag Archipelago. Aleksandr Solzhenitsyn."),
    TEXT("\"Violence can only be concealed by a lie, and the lie can only be maintained by violence.\" The Gulag Archipelago. Aleksandr Solzhenitsyn."),
    TEXT("\"Live not by lies.\" (essay). Aleksandr Solzhenitsyn."),
    TEXT("\"Own only what you can always carry with you: know languages, know countries, know people. Let your memory be your travel bag.\" Aleksandr Solzhenitsyn."),
    TEXT("\"They lie, we know they lie, they know we know they lie, we know they know we know they lie, but they still lie.\" Aleksandr Solzhenitsyn."),
    TEXT("\"Man is born free, and everywhere he is in chains.\" The Social Contract. Jean-Jacques Rousseau."),
    TEXT("\"Freedom is the freedom to say that two plus two make four.\" Nineteen Eighty-Four. George Orwell."),
    TEXT("\"Liberty lies in the rights of that person whose views you find most odious.\" H. L. Mencken."),
    TEXT("\"Between stimulus and response there is a space. In that space is our power to choose our response.\" Viktor Frankl."),
    TEXT("\"Freedom is not worth having if it does not include the freedom to make mistakes.\" Mahatma Gandhi."),
    TEXT("\"The price of freedom is eternal vigilance.\" Thomas Jefferson."),
    TEXT("\"No man is free who is not master of himself.\" Epictetus."),
    TEXT("\"Justice will not be served until those who are unaffected are as outraged as those who are.\" Benjamin Franklin."),
    TEXT("\"The mind is everything. What you think you become.\" (Buddhist teaching). Gautama Buddha."),
    TEXT("\"He who conquers himself is the mightiest warrior.\" Confucius."),
    TEXT("\"To know what is right and not do it is the worst cowardice.\" Confucius."),
    TEXT("\"Freedom lies in being bold.\" Robert Frost."),
    TEXT("\"The quieter you become, the more you are able to hear.\" (Zen saying)."),
    TEXT("\"Wisdom is the reward you get for a lifetime of listening when you would have preferred to talk.\" Mark Twain."),
};

/************************************************************************/

LPCSTR GetRandomQuote(void)
{
    /* Compute number of elements */
    UINT count = (UINT)(sizeof(Quotes) / sizeof(Quotes[0]));

    if (count == 0) {
        return NULL;
    }

    /* Get random index */
    UINT index = RandomInteger() % count;

    return Quotes[index];
}
