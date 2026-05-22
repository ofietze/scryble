1. On the card view: Add a small border around title and the card type
    - If the card type or the title are too long to be displayed, scroll through. them once on page load
    - Then if the user scrolls the description text down and then all the way up, trigger another scroll of the title and type text
2. Fix basic lands not having the color of the mana they produce
2. Add a logo for the app depicting the stylised M ofthe magic the gathering font
3. On the mana value screen remove the helper text at the bottom and instead add an up arrow near the up button and a down arrow near the down button
    - check also if there maybe is a better native implementation
4. Add an extra filter stage after the letter box selection
    - When e.g. A-D is selected, now show a big letter like in the mana selection and an up and down arrow.
    - the user can now select the exact letter from the range they previously picked
5. Add image loading for iOS
    - Advise on best approach
6. Add a third option on the start page "Collection"
    - Cards can be added to the collection from the card view by pressing select. A short pop up message confirms that. Pressing select again removes the card from the collection and shows a pop up as well
    - For the collection we only store the card ids and title. the collection view is a list of all card titles in the collection. Clicking an entry in the list opens the card view for that card. The top of the list view says "X cards in collection". This is the Aonly thing visible if there are no cards in the collection yet
8. Add a readme for humans. Leave some placeholders for three screenshots. Introduce the app in a douglas adams way of making fun of that you're now able to have magic cards on your wrist but why. 
9. Make this app ready to be pushed to a git repo
